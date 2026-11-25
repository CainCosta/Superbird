/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/ChromeStyle.h>
#include <UI/Qt/Icon.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/CommandPalettePopover.h>
#include <UI/Qt/Theme.h>

#include <QFontMetricsF>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QtMath>

namespace Superbird {

static constexpr int POPOVER_WIDTH = 580;
static constexpr int ROW_HEIGHT = 32;
static constexpr int ROW_RADIUS = 6;
static constexpr int ROW_HORIZONTAL_PADDING = 10;
static constexpr int ROW_ICON_SIZE = 16;
static constexpr int ROW_GAP = 10;
static constexpr int CLOSE_BUTTON_SIZE = 16;
static constexpr int LIST_MAX_HEIGHT = 12 * ROW_HEIGHT;

class CommandPaletteList final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit CommandPaletteList(QWidget* parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    Function<void(size_t)> on_activate;
    Function<void(size_t)> on_close_requested;

    void set_rows(Vector<CommandPalettePopover::Row> rows)
    {
        m_rows = AK::move(rows);
        m_highlighted_row = m_rows.is_empty() ? -1 : 0;
        setFixedHeight(static_cast<int>(m_rows.size()) * ROW_HEIGHT);
        update();
    }

    size_t row_count() const { return m_rows.size(); }
    void set_row_detail(size_t row, QString const& detail)
    {
        if (row >= m_rows.size())
            return;
        m_rows[row].detail = detail;
        update();
    }
    int highlighted_row() const { return m_highlighted_row; }
    QRect row_rect(int row) const { return { 0, row * ROW_HEIGHT, width(), ROW_HEIGHT }; }

    void move_highlight(int delta)
    {
        if (m_rows.is_empty())
            return;
        m_highlighted_row = clamp(m_highlighted_row + delta, 0, static_cast<int>(m_rows.size()) - 1);
        update();
    }

    void activate_highlighted_row()
    {
        if (m_highlighted_row >= 0 && on_activate)
            on_activate(static_cast<size_t>(m_highlighted_row));
    }

    void close_highlighted_row()
    {
        if (m_highlighted_row >= 0 && m_rows[m_highlighted_row].can_close && on_close_requested)
            on_close_requested(static_cast<size_t>(m_highlighted_row));
    }

private:
    QRect close_button_rect(int row) const
    {
        auto rect = row_rect(row);
        return { rect.right() + 1 - ROW_HORIZONTAL_PADDING - CLOSE_BUTTON_SIZE, rect.center().y() - CLOSE_BUTTON_SIZE / 2 + 1, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE };
    }

    // Draws the title with the matched characters in the accent color and bold, unless it has to be elided.
    static void paint_title(QPainter& painter, QRect const& rect, QFont const& font, CommandPalettePopover::Row const& row, QColor const& text, QColor const& accent)
    {
        QFontMetrics metrics(font);
        if (row.highlights.is_empty() || metrics.horizontalAdvance(row.title) > rect.width()) {
            painter.setFont(font);
            painter.setPen(text);
            painter.drawText(rect, Qt::AlignVCenter | Qt::AlignLeft, metrics.elidedText(row.title, Qt::ElideRight, rect.width()));
            return;
        }

        auto bold_font = font;
        bold_font.setBold(true);
        QFontMetrics bold_metrics(bold_font);

        auto is_highlighted = [&](int index) { return row.highlights.contains_slow(index); };
        int x = rect.left();
        int index = 0;
        while (index < row.title.size()) {
            auto highlighted = is_highlighted(index);
            int end = index + 1;
            while (end < row.title.size() && is_highlighted(end) == highlighted)
                ++end;
            auto segment = row.title.mid(index, end - index);
            auto const& segment_metrics = highlighted ? bold_metrics : metrics;
            auto segment_width = segment_metrics.horizontalAdvance(segment);
            if (x + segment_width > rect.right() + 1) {
                // Bold glyphs may push the end past the room; finish the line plainly.
                painter.setFont(font);
                painter.setPen(text);
                painter.drawText(QRect(x, rect.top(), rect.right() + 1 - x, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, metrics.elidedText(row.title.mid(index), Qt::ElideRight, rect.right() + 1 - x));
                return;
            }
            painter.setFont(highlighted ? bold_font : font);
            painter.setPen(highlighted ? accent : text);
            painter.drawText(QRect(x, rect.top(), segment_width, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, segment);
            x += segment_width;
            index = end;
        }
    }

    virtual void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto const& panel = Ladybird::Theme::panel();
        auto accent = Ladybird::Theme::accent();

        auto title_font = font();
        auto detail_font = font();
        detail_font.setPixelSize(12);
        QFontMetricsF detail_metrics(detail_font);

        for (size_t index = 0; index < m_rows.size(); ++index) {
            auto const& row = m_rows[index];
            auto row_index = static_cast<int>(index);
            auto rect = row_rect(row_index);
            auto highlighted = row_index == m_highlighted_row;

            if (highlighted) {
                QPainterPath path;
                path.addRoundedRect(QRectF(rect), ROW_RADIUS, ROW_RADIUS);
                painter.fillPath(path, panel.pressed);
            }

            auto x = rect.left() + ROW_HORIZONTAL_PADDING;
            QRect icon_rect { x, rect.center().y() - (ROW_ICON_SIZE / 2) + 1, ROW_ICON_SIZE, ROW_ICON_SIZE };
            auto icon = row.icon.isNull() ? Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Globe, panel.muted) : row.icon;
            icon.paint(&painter, icon_rect);
            x += ROW_ICON_SIZE + ROW_GAP;

            auto right = rect.right() + 1 - ROW_HORIZONTAL_PADDING;
            if (highlighted && row.can_close) {
                auto close_rect = close_button_rect(row_index);
                Ladybird::create_chrome_icon(Ladybird::ChromeIcon::TabClose, panel.muted).paint(&painter, close_rect);
                right = close_rect.left() - ROW_GAP / 2;
            }

            auto detail_width = min(qCeil(detail_metrics.horizontalAdvance(row.detail)), (right - x) / 2);
            painter.setFont(detail_font);
            painter.setPen(row.is_error ? panel.warning : panel.muted);
            painter.drawText(QRect(right - detail_width, rect.top(), detail_width, rect.height()), Qt::AlignVCenter | Qt::AlignRight, detail_metrics.elidedText(row.detail, Qt::ElideMiddle, detail_width));

            auto title_width = right - detail_width - ROW_GAP - x;
            paint_title(painter, QRect(x, rect.top(), title_width, rect.height()), title_font, row, row.is_error ? panel.warning : panel.text, accent);
        }
    }

    virtual void mouseMoveEvent(QMouseEvent* event) override
    {
        auto row = event->position().toPoint().y() / ROW_HEIGHT;
        if (row >= 0 && row < static_cast<int>(m_rows.size()) && row != m_highlighted_row) {
            m_highlighted_row = row;
            update();
        }
    }

    virtual void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !rect().contains(event->position().toPoint()))
            return;
        auto row = event->position().toPoint().y() / ROW_HEIGHT;
        if (row < 0 || row >= static_cast<int>(m_rows.size()))
            return;
        m_highlighted_row = row;
        if (m_rows[row].can_close && close_button_rect(row).contains(event->position().toPoint())) {
            close_highlighted_row();
            return;
        }
        activate_highlighted_row();
    }

    Vector<CommandPalettePopover::Row> m_rows;
    int m_highlighted_row { -1 };
};

CommandPalettePopover::CommandPalettePopover(QWidget* parent)
    : Popover(parent)
{
    setAccessibleName("Command Palette");
    // NB: The All Tabs popover's object names pick up its style sheet, so the palette matches Ladybird's own popovers.
    card().setObjectName("LadybirdAllTabsPopover");
    card().setFixedWidth(POPOVER_WIDTH);

    auto* layout = new QVBoxLayout(&card());
    layout->setContentsMargins(6, 8, 6, 6);
    layout->setSpacing(0);

    m_field = new QLineEdit(&card());
    m_field->setObjectName("LadybirdAllTabsSearch");
    m_field->setAccessibleName("Command Palette search");
    m_field->setPlaceholderText("Search tabs, bookmarks, history and commands \xE2\x80\x94 or type = to calculate");
    m_field->addAction(Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Search, Ladybird::Theme::panel().muted), QLineEdit::LeadingPosition);
    m_field->installEventFilter(this);
    QObject::connect(m_field, &QLineEdit::textChanged, this, [this](QString const& text) {
        if (on_query_changed)
            on_query_changed(text);
    });
    auto* field_row = new QHBoxLayout;
    field_row->setContentsMargins(4, 0, 4, 0);
    field_row->addWidget(m_field);
    layout->addLayout(field_row);
    layout->addSpacing(8);

    m_list = new CommandPaletteList(&card());
    m_list->on_activate = [this](size_t index) {
        if (on_activate)
            on_activate(index);
    };
    m_list->on_close_requested = [this](size_t index) {
        if (on_close_requested)
            on_close_requested(index);
    };

    m_scroll_area = new QScrollArea(&card());
    m_scroll_area->setObjectName("LadybirdAllTabsList");
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setWidget(m_list);
    m_list->setAutoFillBackground(false);
    m_scroll_area->viewport()->setAutoFillBackground(false);
    layout->addWidget(m_scroll_area);

#if defined(AK_OS_MACOS)
    auto close_tab_shortcut = QStringLiteral("\xE2\x8C\x98\xE2\x8C\xAB");
#else
    auto close_tab_shortcut = QStringLiteral("Ctrl+Backspace");
#endif
#if defined(AK_OS_MACOS)
    auto new_tab_shortcut = QStringLiteral("\xE2\x8C\x98\xE2\x86\xB5");
#else
    auto new_tab_shortcut = QStringLiteral("Ctrl+\xE2\x86\xB5");
#endif
    m_hint = new QLabel(QString("\xE2\x86\xB5 open  \xC2\xB7  %1 new tab  \xC2\xB7  %2 close tab  \xC2\xB7  = calculate  \xC2\xB7  esc dismiss").arg(new_tab_shortcut, close_tab_shortcut), &card());
    m_hint->setObjectName("LadybirdAllTabsHeading");
    m_hint->setContentsMargins(10, 6, 10, 2);
    layout->addWidget(m_hint);
}

void CommandPalettePopover::set_rows(Vector<Row> rows)
{
    m_list->set_rows(AK::move(rows));
    m_scroll_area->setFixedHeight(min(LIST_MAX_HEIGHT, max(m_list->height(), 1)));
    m_scroll_area->verticalScrollBar()->setValue(0);
    card().adjustSize();
    adjustSize();
}

void CommandPalettePopover::set_row_detail(size_t row, QString const& detail)
{
    m_list->set_row_detail(row, detail);
}

void CommandPalettePopover::clear_query()
{
    m_field->clear();
}

QString CommandPalettePopover::query() const
{
    return m_field->text();
}

void CommandPalettePopover::update_chrome_style(QPalette const& palette)
{
    setPalette(palette);
    setStyleSheet(Ladybird::ChromeStyle::all_tabs_popover_style_sheet(palette));
    m_list->update();
}

void CommandPalettePopover::showEvent(QShowEvent* event)
{
    Popover::showEvent(event);
    m_field->setFocus();
}

void CommandPalettePopover::ensure_highlight_visible()
{
    if (auto row = m_list->highlighted_row(); row >= 0)
        m_scroll_area->ensureVisible(0, m_list->row_rect(row).center().y(), 0, ROW_HEIGHT);
}

bool CommandPalettePopover::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_field && event->type() == QEvent::KeyPress) {
        auto const& key_event = *static_cast<QKeyEvent*>(event);
        auto key = key_event.key();
        switch (key) {
        case Qt::Key_Up:
        case Qt::Key_Down:
            m_list->move_highlight(key == Qt::Key_Up ? -1 : 1);
            ensure_highlight_visible();
            return true;
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            m_list->move_highlight(key == Qt::Key_PageUp ? -8 : 8);
            ensure_highlight_visible();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // NB: Qt reports Command as Control on macOS.
            if (key_event.modifiers().testFlag(Qt::ControlModifier) && on_activate_in_new_tab) {
                if (auto row = m_list->highlighted_row(); row >= 0)
                    on_activate_in_new_tab(static_cast<size_t>(row));
                return true;
            }
            m_list->activate_highlighted_row();
            return true;
        case Qt::Key_Backspace:
            // NB: Qt reports Command as Control on macOS, so this is Cmd+Backspace there and Ctrl+Backspace elsewhere.
            if (key_event.modifiers().testFlag(Qt::ControlModifier)) {
                m_list->close_highlighted_row();
                return true;
            }
            break;
        default:
            break;
        }
    }
    return Popover::eventFilter(watched, event);
}

}
