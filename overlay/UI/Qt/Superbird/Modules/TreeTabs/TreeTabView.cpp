/*
 * Copyright (c) 2026, Sidelady contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StdLibExtras.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/ChromeStyle.h>
#include <UI/Qt/Icon.h>
#include <UI/Qt/Menu.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/Theme.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabController.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabFilter.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabView.h>

#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

namespace Ladybird {

static constexpr int TREE_ROW_HEIGHT = 31;
static constexpr qreal TREE_ROW_RADIUS = 6.0;
static constexpr int TREE_ROW_HORIZONTAL_INSET = 5;
static constexpr int TREE_ROW_VERTICAL_GAP = 1;
static constexpr int TREE_INDENT_PER_LEVEL = 16;
static constexpr size_t TREE_MAX_VISUAL_DEPTH = 8;
static constexpr int TREE_TWISTY_SIZE = 16;
static constexpr int TREE_FAVICON_SIZE = 16;
static constexpr int TREE_BUTTON_SIZE = 20;
static constexpr int TREE_TOP_PADDING = 2;
static constexpr int TREE_PINNED_TILE_HEIGHT = 32;
static constexpr int TREE_PINNED_TILE_GAP = 4;
static constexpr int TREE_PINNED_SEPARATOR_HEIGHT = 13;
static constexpr int TREE_PINNED_COLUMNS = 4;
static constexpr qreal TREE_UNLOADED_OPACITY = 0.5;
static constexpr int TREE_DRAG_AUTO_EXPAND_MS = 400;
static constexpr int TREE_DETACH_MARGIN_PX = 48;
static constexpr int TREE_FILTER_BAR_HEIGHT = 34;
static constexpr int TREE_COLOR_BAR_WIDTH = 3;

QColor branch_color_to_qcolor(BranchColor color)
{
    switch (color) {
    case BranchColor::None:
        return {};
    case BranchColor::Red:
        return QColor(0xE5484D);
    case BranchColor::Orange:
        return QColor(0xF76B15);
    case BranchColor::Yellow:
        return QColor(0xE2A336);
    case BranchColor::Green:
        return QColor(0x30A46C);
    case BranchColor::Teal:
        return QColor(0x12A594);
    case BranchColor::Blue:
        return QColor(0x0090FF);
    case BranchColor::Purple:
        return QColor(0x8E4EC6);
    case BranchColor::Pink:
        return QColor(0xD6409F);
    }
    return {};
}

// Mirrors Sidebery v5's tab surface styling (vars.styl / sidebar/tab.styl):
//   - Hovered inactive tab: subtle translucent overlay (#ffffff16 dark / #00000012 light)
//   - Active tab: lifted surface (#ffffff24 dark / #ffffff light) with soft drop shadow and 1px inset edge
//     (and no extra hover wash on top of an already-active card)
//   - Multi-selected tab: translucent selected fill + 1px accent inset border (2px when also active)
static void paint_sidebery_tab_background(
    QPainter& painter,
    QRectF const& rect,
    QPalette const& chrome_palette,
    bool is_active,
    bool is_hovered,
    bool is_multi_selected)
{
    if (!is_active && !is_hovered && !is_multi_selected)
        return;

    auto const& colors = Theme::chrome(chrome_palette);
    bool dark = Theme::is_dark(chrome_palette);

    painter.save();
    QPainterPath card_path;
    card_path.addRoundedRect(rect, TREE_ROW_RADIUS, TREE_ROW_RADIUS);

    if (is_active) {
        // Soft drop shadow (Sidebery --active-el-shadow: 0 1px 4px -1px #00000048 / #00000052)
        auto shadow_rect = rect.translated(0.0, 1.0);
        QPainterPath outer_shadow;
        outer_shadow.addRoundedRect(shadow_rect.adjusted(-0.5, 0.0, 0.5, 1.0), TREE_ROW_RADIUS + 0.5, TREE_ROW_RADIUS + 0.5);
        QPainterPath inner_shadow;
        inner_shadow.addRoundedRect(shadow_rect, TREE_ROW_RADIUS, TREE_ROW_RADIUS);
        painter.fillPath(outer_shadow, QColor(0, 0, 0, dark ? 0x1E : 0x16));
        painter.fillPath(inner_shadow, QColor(0, 0, 0, dark ? 0x2E : 0x20));

        // Active surface (--active-el-bg: #ffffff24 dark / #ffffff light)
        QColor active_fill = dark ? QColor(255, 255, 255, 0x24) : colors.toolbar;
        painter.fillPath(card_path, active_fill);

        // Crisp 1px inset border (--active-el-shadow inset 0 0 0 1px #ffffff08 / increased-contrast edge)
        QColor edge = colors.active_tab_edge.alpha() > 0
            ? colors.active_tab_edge
            : (dark ? QColor(255, 255, 255, 0x12) : QColor(0, 0, 0, 0x14));
        QPainterPath inset_path;
        inset_path.addRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), TREE_ROW_RADIUS - 0.5, TREE_ROW_RADIUS - 0.5);
        painter.setPen(QPen(edge, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(inset_path);
    } else if (is_multi_selected) {
        // Selected overlay (--frame-el-overlay-selected-bg: #ffffff24 dark / #00000012 light)
        painter.fillPath(card_path, dark ? QColor(255, 255, 255, 0x24) : QColor(0, 0, 0, 0x12));
    } else if (is_hovered) {
        // Hover overlay (--frame-el-overlay-hover-bg: #ffffff16 dark / #00000012 light)
        painter.fillPath(card_path, dark ? QColor(255, 255, 255, 0x16) : QColor(0, 0, 0, 0x12));
    }

    if (is_multi_selected) {
        qreal border_width = is_active ? 2.0 : 1.0;
        qreal inset = border_width / 2.0;
        QPainterPath sel_path;
        sel_path.addRoundedRect(rect.adjusted(inset, inset, -inset, -inset), TREE_ROW_RADIUS - inset, TREE_ROW_RADIUS - inset);
        painter.setPen(QPen(Theme::accent(), border_width));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(sel_path);
    }

    painter.restore();
}

TreeTabView::TreeTabView(TreeTabController& controller, QWidget* parent)
    : QAbstractScrollArea(parent)
    , m_controller(controller)
{
    setObjectName("LadybirdTreeTabView");
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::ClickFocus);
    // NB: The tree is only as tall as its rows, so the new tab button can sit right under the last one. With more rows
    //     than room, it shrinks to what's left of the sidebar and scrolls.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(false);

    m_auto_expand_timer = new QTimer(this);
    m_auto_expand_timer->setSingleShot(true);
    m_auto_expand_timer->setInterval(TREE_DRAG_AUTO_EXPAND_MS);
    connect(m_auto_expand_timer, &QTimer::timeout, this, [this] {
        if (!m_dragging || !m_auto_expand_node_id.has_value())
            return;
        if (auto const* node = m_controller.model().node(*m_auto_expand_node_id); node && node->tab)
            m_controller.set_branch_collapsed(*node->tab, false);
    });

    // The quick filter sits above the tree, in the scroll area's top margin.
    m_filter_field = new QLineEdit(this);
    m_filter_field->setObjectName("SuperbirdTreeTabFilter");
    m_filter_field->setPlaceholderText("Filter tabs");
    m_filter_field->setAccessibleName("Filter tabs");
    m_filter_field->setClearButtonEnabled(true);
    m_filter_field->installEventFilter(this);
    connect(m_filter_field, &QLineEdit::textChanged, this, [this](QString const& text) {
        m_controller.set_filter_text(text);
        verticalScrollBar()->setValue(0);
    });

    m_loading_repaint_timer = new QTimer(this);
    m_loading_repaint_timer->setInterval(33);
    connect(m_loading_repaint_timer, &QTimer::timeout, this, [this] {
        if (isVisible())
            viewport()->update();
    });

    update_chrome_style();
}

TreeTabView::~TreeTabView() = default;

void TreeTabView::rebuild_rows()
{
    m_rows = m_controller.visible_rows();
    update_size_hint();
    update_scrollbars();
    update_loading_timer();
    viewport()->update();
}

void TreeTabView::update_chrome_style()
{
    setStyleSheet("QAbstractScrollArea { background: transparent; border: none; }"
                  "QScrollBar:vertical { background: transparent; width: 6px; margin: 2px 1px 2px 0; }"
                  "QScrollBar::handle:vertical { background: rgba(128, 128, 128, 0.35); border-radius: 3px; min-height: 24px; }"
                  "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }");
    if (m_filter_field) {
        auto chrome_palette = Theme::chrome_palette(*this);
        auto const& colors = Theme::chrome(chrome_palette);
        m_filter_field->setStyleSheet(QString("QLineEdit#SuperbirdTreeTabFilter { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 2px 6px; }"
                                              "QLineEdit#SuperbirdTreeTabFilter:focus { border-color: %4; }")
                                          .arg(ChromeStyle::style_sheet_color(colors.field), ChromeStyle::style_sheet_color(colors.text),
                                              ChromeStyle::style_sheet_color(colors.field_edge.alpha() > 0 ? colors.field_edge : colors.separator),
                                              ChromeStyle::style_sheet_color(Theme::focus_edge())));
        for (auto* action : m_filter_field->actions())
            m_filter_field->removeAction(action);
        m_filter_field->addAction(create_chrome_icon(ChromeIcon::Search, colors.muted), QLineEdit::LeadingPosition);
    }
    update_filter_bar();
    viewport()->update();
}

void TreeTabView::update_filter_bar()
{
    if (!m_filter_field)
        return;
    // The collapsed rail has no room for a field; its filter is put aside until the sidebar opens again.
    auto shown = !m_collapsed_rail;
    m_filter_field->setVisible(shown);
    setViewportMargins(0, shown ? TREE_FILTER_BAR_HEIGHT : 0, 0, 0);
    if (shown)
        m_filter_field->setGeometry(TREE_ROW_HORIZONTAL_INSET, 4, max(0, width() - (2 * TREE_ROW_HORIZONTAL_INSET)), TREE_FILTER_BAR_HEIGHT - 8);
    if (!shown && !m_filter_field->text().isEmpty())
        m_filter_field->clear();
}

void TreeTabView::focus_filter()
{
    if (!m_filter_field || !m_filter_field->isVisible())
        return;
    m_filter_field->setFocus(Qt::ShortcutFocusReason);
    m_filter_field->selectAll();
}

bool TreeTabView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_filter_field && event->type() == QEvent::KeyPress) {
        auto key = static_cast<QKeyEvent*>(event)->key();
        // Down moves from the field into the tree, like a search field over a list.
        if (key == Qt::Key_Down) {
            setFocus(Qt::TabFocusReason);
            return true;
        }
        if (key == Qt::Key_Escape) {
            m_filter_field->clear();
            if (auto* tab = m_controller.window().current_tab(); tab && tab->is_loaded())
                tab->view().setFocus(Qt::OtherFocusReason);
            return true;
        }
        // Enter goes to the first tab the filter shows.
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            auto query = m_filter_field->text().trimmed().toCaseFolded().toUtf8();
            for (auto const& row : m_rows) {
                if (!row.tab)
                    continue;
                auto title = row.tab->title().toCaseFolded().toUtf8();
                auto url = QString::fromUtf8(row.tab->url().to_byte_string().characters()).toCaseFolded().toUtf8();
                if (Superbird::TreeTabs::tab_matches_filter({ title.constData(), static_cast<size_t>(title.size()) }, { url.constData(), static_cast<size_t>(url.size()) }, { query.constData(), static_cast<size_t>(query.size()) })) {
                    m_controller.activate_tab(*row.tab);
                    break;
                }
            }
            return true;
        }
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

void TreeTabView::set_collapsed_rail(bool collapsed_rail)
{
    if (m_collapsed_rail == collapsed_rail)
        return;
    m_collapsed_rail = collapsed_rail;
    update_filter_bar();
    update_size_hint();
    update_scrollbars();
    viewport()->update();
}

void TreeTabView::ensure_tab_visible(Tab const* tab)
{
    if (!tab)
        return;

    auto pinned = compute_pinned_layout();
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].tab != tab)
            continue;
        auto y_top = static_cast<int>(i) * TREE_ROW_HEIGHT;
        auto y_bottom = y_top + TREE_ROW_HEIGHT;
        auto scroll_y = verticalScrollBar()->value();
        auto visible_height = max(0, viewport()->height() - pinned.total_height - TREE_TOP_PADDING);
        if (y_top < scroll_y)
            verticalScrollBar()->setValue(y_top);
        else if (y_bottom > scroll_y + visible_height)
            verticalScrollBar()->setValue(max(0, y_bottom - visible_height));
        return;
    }
}

int TreeTabView::filter_bar_height() const
{
    return m_collapsed_rail ? 0 : TREE_FILTER_BAR_HEIGHT;
}

QSize TreeTabView::sizeHint() const
{
    auto pinned = compute_pinned_layout();
    auto height = filter_bar_height() + TREE_TOP_PADDING + pinned.total_height + static_cast<int>(m_rows.size()) * TREE_ROW_HEIGHT + 4;
    return { 220, height };
}

QSize TreeTabView::minimumSizeHint() const
{
    return { 36, filter_bar_height() + TREE_ROW_HEIGHT };
}

void TreeTabView::update_size_hint()
{
    // NB: Tabs coming and going, branches folding and the pinned tiles rewrapping all change how tall the tree wants to be.
    auto height = sizeHint().height();
    if (height == m_size_hint_height)
        return;
    m_size_hint_height = height;
    updateGeometry();
}

TreeTabView::PinnedLayout TreeTabView::compute_pinned_layout() const
{
    PinnedLayout layout;
    layout.tabs = m_controller.pinned_tabs();
    if (layout.tabs.is_empty())
        return layout;

    auto available_w = max(1, viewport()->width() - 2 * TREE_ROW_HORIZONTAL_INSET);
    layout.columns = m_collapsed_rail ? 1 : max(1, min(TREE_PINNED_COLUMNS, available_w / 40));
    auto count = static_cast<int>(layout.tabs.size());
    auto rows = (count + layout.columns - 1) / layout.columns;
    layout.tile_width = max(24, (available_w - (layout.columns - 1) * TREE_PINNED_TILE_GAP) / layout.columns);
    layout.total_height = rows * TREE_PINNED_TILE_HEIGHT + max(0, rows - 1) * TREE_PINNED_TILE_GAP + TREE_PINNED_SEPARATOR_HEIGHT;
    return layout;
}

QRect TreeTabView::pinned_tile_rect(PinnedLayout const& layout, int index) const
{
    auto col = index % layout.columns;
    auto row = index / layout.columns;
    auto x = TREE_ROW_HORIZONTAL_INSET + col * (layout.tile_width + TREE_PINNED_TILE_GAP);
    auto y = TREE_TOP_PADDING + row * (TREE_PINNED_TILE_HEIGHT + TREE_PINNED_TILE_GAP);
    return { x, y, layout.tile_width, TREE_PINNED_TILE_HEIGHT };
}

QRect TreeTabView::tree_row_rect(int row_index, int pinned_height) const
{
    auto y = TREE_TOP_PADDING + pinned_height + row_index * TREE_ROW_HEIGHT - verticalScrollBar()->value();
    return { TREE_ROW_HORIZONTAL_INSET, y, max(0, viewport()->width() - 2 * TREE_ROW_HORIZONTAL_INSET), TREE_ROW_HEIGHT - TREE_ROW_VERTICAL_GAP };
}

QRect TreeTabView::twisty_rect_for_row(QRect const& row_rect, size_t depth) const
{
    auto clamped_depth = min(depth, TREE_MAX_VISUAL_DEPTH);
    auto indent = static_cast<int>(clamped_depth) * TREE_INDENT_PER_LEVEL;
    auto x = row_rect.left() + 6 + indent;
    auto y = row_rect.top() + (row_rect.height() - TREE_TWISTY_SIZE) / 2;
    return { x, y, TREE_TWISTY_SIZE, TREE_TWISTY_SIZE };
}

QRect TreeTabView::close_button_rect_for_row(QRect const& row_rect) const
{
    auto x = row_rect.right() - TREE_BUTTON_SIZE - 4;
    auto y = row_rect.top() + (row_rect.height() - TREE_BUTTON_SIZE) / 2;
    return { x, y, TREE_BUTTON_SIZE, TREE_BUTTON_SIZE };
}

QRect TreeTabView::audio_button_rect_for_row(QRect const& row_rect, bool has_close_button) const
{
    auto trailing = has_close_button ? (TREE_BUTTON_SIZE + 6) : 4;
    auto x = row_rect.right() - trailing - TREE_BUTTON_SIZE;
    auto y = row_rect.top() + (row_rect.height() - TREE_BUTTON_SIZE) / 2;
    return { x, y, TREE_BUTTON_SIZE, TREE_BUTTON_SIZE };
}

TreeTabView::HitResult TreeTabView::hit_test(QPoint const& viewport_pos) const
{
    auto pinned = compute_pinned_layout();
    for (int i = 0; i < static_cast<int>(pinned.tabs.size()); ++i) {
        if (pinned_tile_rect(pinned, i).contains(viewport_pos))
            return { .is_pinned = true, .index = i, .part = RowHitPart::Row };
    }

    auto tree_top = TREE_TOP_PADDING + pinned.total_height;
    if (viewport_pos.y() < tree_top)
        return {};

    auto virtual_y = viewport_pos.y() - tree_top + verticalScrollBar()->value();
    auto row_index = virtual_y / TREE_ROW_HEIGHT;
    if (row_index < 0 || row_index >= static_cast<int>(m_rows.size()))
        return {};

    auto rect = tree_row_rect(row_index, pinned.total_height);
    if (!rect.contains(viewport_pos))
        return {};

    if (m_collapsed_rail)
        return { .is_pinned = false, .index = row_index, .part = RowHitPart::Row };

    auto const& row = m_rows[static_cast<size_t>(row_index)];
    if (row.has_children && twisty_rect_for_row(rect, row.depth).adjusted(-3, -4, 3, 4).contains(viewport_pos))
        return { .is_pinned = false, .index = row_index, .part = RowHitPart::Twisty };

    auto is_active = row.tab == m_controller.window().current_tab();
    auto shows_close = is_active || (m_hover.index == row_index && !m_hover.is_pinned);
    if (shows_close && close_button_rect_for_row(rect).contains(viewport_pos))
        return { .is_pinned = false, .index = row_index, .part = RowHitPart::CloseButton };

    if (row.tab && TreeTabController::tab_has_audio(*row.tab) && audio_button_rect_for_row(rect, shows_close).contains(viewport_pos))
        return { .is_pinned = false, .index = row_index, .part = RowHitPart::AudioButton };

    return { .is_pinned = false, .index = row_index, .part = RowHitPart::Row };
}

void TreeTabView::update_scrollbars()
{
    auto pinned = compute_pinned_layout();
    auto content_height = static_cast<int>(m_rows.size()) * TREE_ROW_HEIGHT + 4;
    auto available_height = max(0, viewport()->height() - TREE_TOP_PADDING - pinned.total_height);
    auto max_scroll = max(0, content_height - available_height);
    verticalScrollBar()->setPageStep(max(TREE_ROW_HEIGHT, available_height));
    verticalScrollBar()->setSingleStep(TREE_ROW_HEIGHT);
    verticalScrollBar()->setRange(0, max_scroll);
}

void TreeTabView::update_loading_timer()
{
    bool any_loading = false;
    for (auto const& row : m_rows) {
        if (row.tab && (row.tab->is_loading() || row.tab->loading_ring_opacity() > 0.01)) {
            any_loading = true;
            break;
        }
    }
    if (!any_loading) {
        for (auto* tab : m_controller.pinned_tabs()) {
            if (tab && (tab->is_loading() || tab->loading_ring_opacity() > 0.01)) {
                any_loading = true;
                break;
            }
        }
    }

    if (any_loading && !m_loading_repaint_timer->isActive())
        m_loading_repaint_timer->start();
    else if (!any_loading && m_loading_repaint_timer->isActive())
        m_loading_repaint_timer->stop();
}

void TreeTabView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    update_filter_bar();
    update_size_hint();
    update_scrollbars();
}

void TreeTabView::paint_pinned_region(QPainter& painter, PinnedLayout const& layout, QPalette const& chrome_palette)
{
    if (layout.tabs.is_empty())
        return;

    auto const& colors = Theme::chrome(chrome_palette);
    auto* current_tab = m_controller.window().current_tab();

    for (int i = 0; i < static_cast<int>(layout.tabs.size()); ++i) {
        auto* tab = layout.tabs[static_cast<size_t>(i)];
        if (!tab)
            continue;

        auto rect = pinned_tile_rect(layout, i);
        bool is_active = (tab == current_tab);
        bool is_hovered = (m_hover.is_pinned && m_hover.index == i);

        paint_sidebery_tab_background(painter, QRectF(rect), chrome_palette, is_active, is_hovered, false);

        auto icon_rect = QRect(
            rect.center().x() - TREE_FAVICON_SIZE / 2,
            rect.center().y() - TREE_FAVICON_SIZE / 2,
            TREE_FAVICON_SIZE,
            TREE_FAVICON_SIZE);

        painter.save();
        if (!tab->is_loaded())
            painter.setOpacity(TREE_UNLOADED_OPACITY);
        auto icon = tab->tab_icon();
        if (icon.isNull())
            icon = create_chrome_icon(ChromeIcon::Globe, chrome_palette);
        icon.paint(&painter, icon_rect);
        painter.restore();

        if (tab->loading_ring_opacity() > 0.01) {
            painter.save();
            painter.setOpacity(tab->loading_ring_opacity());
            auto phase = tab->is_waiting_for_response() ? ChromeStyle::LoadingPhase::WaitingForResponse : ChromeStyle::LoadingPhase::Receiving;
            ChromeStyle::paint_loading_ring(painter, chrome_palette, QPointF(rect.center()), ChromeStyle::LoadingRingSize::Favicon, phase);
            painter.restore();
        }

        if (tab->needs_attention() && !is_active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Theme::accent());
            painter.drawEllipse(QPointF(rect.right() - 6, rect.top() + 6), 3.0, 3.0);
        }
    }

    auto sep_y = TREE_TOP_PADDING + layout.total_height - (TREE_PINNED_SEPARATOR_HEIGHT / 2);
    painter.setPen(QPen(colors.separator, 1.0));
    painter.drawLine(TREE_ROW_HORIZONTAL_INSET + 4, sep_y, viewport()->width() - TREE_ROW_HORIZONTAL_INSET - 4, sep_y);
}

void TreeTabView::paint_tree_row(QPainter& painter, int row_index, QRect const& row_rect, QPalette const& chrome_palette)
{
    auto const& row = m_rows[static_cast<size_t>(row_index)];
    auto* tab = row.tab;
    if (!tab)
        return;

    auto const& colors = Theme::chrome(chrome_palette);
    auto dpr = devicePixelRatioF();
    bool is_active = (tab == m_controller.window().current_tab());
    bool is_hovered = (!m_hover.is_pinned && m_hover.index == row_index);
    bool is_multi_selected = row.is_selected && m_controller.model().selected_count() > 1;

    // 1. Row card background (Sidebery-style active / multi-selected / hovered)
    paint_sidebery_tab_background(painter, QRectF(row_rect), chrome_palette, is_active, is_hovered, is_multi_selected);

    // A branch color tag shows as a slim bar at the row's leading edge.
    if (row.color != BranchColor::None) {
        auto tag_color = branch_color_to_qcolor(row.color);
        tag_color.setAlphaF(is_active ? 1.0 : 0.85);
        QPainterPath tag_bar;
        tag_bar.addRoundedRect(QRectF(row_rect.left() + 1, row_rect.top() + 4, TREE_COLOR_BAR_WIDTH, row_rect.height() - 8), 1.5, 1.5);
        painter.fillPath(tag_bar, tag_color);
    }

    // Active descendant highlight bar on a collapsed parent
    if (row.collapsed && row.has_active_descendant && !is_active) {
        QPainterPath accent_bar;
        accent_bar.addRoundedRect(QRectF(row_rect.left() + 1, row_rect.top() + 5, 3, row_rect.height() - 10), 1.5, 1.5);
        painter.fillPath(accent_bar, Theme::accent());
    }

    // A tab another module groups (in a Split View, say) wears that group's color at the row's trailing edge.
    if (auto marker = Superbird::WindowHost::tab_marker_color(*tab); marker.has_value()) {
        QPainterPath marker_bar;
        marker_bar.addRoundedRect(QRectF(row_rect.right() - 3.5, row_rect.top() + 7, 2.5, row_rect.height() - 14), 1.25, 1.25);
        painter.fillPath(marker_bar, *marker);
    }

    // Collapsed rail mode: compact centered favicon + tiny nesting bar
    if (m_collapsed_rail) {
        if (row.depth > 0) {
            auto bar_color = colors.separator;
            bar_color.setAlphaF(0.8);
            painter.fillRect(QRect(row_rect.left() + 2, row_rect.top() + 7, 2, row_rect.height() - 14), bar_color);
        }
        auto icon_rect = QRect(
            row_rect.center().x() - TREE_FAVICON_SIZE / 2,
            row_rect.center().y() - TREE_FAVICON_SIZE / 2,
            TREE_FAVICON_SIZE,
            TREE_FAVICON_SIZE);
        painter.save();
        if (!tab->is_loaded())
            painter.setOpacity(TREE_UNLOADED_OPACITY);
        auto icon = tab->tab_icon();
        if (icon.isNull())
            icon = create_chrome_icon(ChromeIcon::Globe, chrome_palette);
        icon.paint(&painter, icon_rect);
        painter.restore();
        return;
    }

    // 2. Sidebery-style branch lines: one continuous vertical line per ancestor level, running under the
    //    parent's chevron down the left of its whole visible subtree, inset where the branch starts and ends.
    auto clamped_depth = min(row.depth, TREE_MAX_VISUAL_DEPTH);
    if (clamped_depth > 0) {
        static constexpr qreal BRANCH_LINE_WIDTH = 1.5;
        static constexpr qreal BRANCH_LINE_END_INSET = 4.0;

        auto previous_depth = row_index > 0 ? min(m_rows[static_cast<size_t>(row_index - 1)].depth, TREE_MAX_VISUAL_DEPTH) : 0uz;
        auto next_index = static_cast<size_t>(row_index) + 1;
        auto next_depth = next_index < m_rows.size() ? min(m_rows[next_index].depth, TREE_MAX_VISUAL_DEPTH) : 0uz;

        auto line_color = colors.muted;
        line_color.setAlphaF(Theme::is_dark(chrome_palette) ? 0.40 : 0.45);
        painter.save();
        painter.setPen(QPen(line_color, BRANCH_LINE_WIDTH, Qt::SolidLine, Qt::RoundCap));
        for (size_t d = 0; d < clamped_depth; ++d) {
            // The row directly above sits at this level only when it is the branch's parent.
            bool starts_branch = previous_depth <= d;
            // The branch ends when the next row climbs back out to this level or above.
            bool ends_branch = next_depth <= d;

            qreal line_x = row_rect.left() + 6 + static_cast<int>(d) * TREE_INDENT_PER_LEVEL + (TREE_TWISTY_SIZE / 2.0);
            qreal top_y = starts_branch ? row_rect.top() + 1.0 : row_rect.top() - TREE_ROW_VERTICAL_GAP;
            qreal bottom_y = ends_branch ? row_rect.bottom() - BRANCH_LINE_END_INSET : row_rect.bottom() + TREE_ROW_VERTICAL_GAP;
            painter.drawLine(QPointF(line_x, top_y), QPointF(line_x, bottom_y));
        }
        painter.restore();
    }

    // 3. Twisty chevron for parent tabs
    auto twisty_rect = twisty_rect_for_row(row_rect, row.depth);
    if (row.has_children) {
        bool twisty_hovered = is_hovered && m_hover.part == RowHitPart::Twisty;
        if (twisty_hovered) {
            QPainterPath bg;
            bg.addRoundedRect(QRectF(twisty_rect).adjusted(-1, -1, 1, 1), 4, 4);
            painter.fillPath(bg, colors.strip_hover);
        }
        auto chevron_color = twisty_hovered ? colors.text : colors.muted;
        auto chevron = create_chrome_icon_pixmap(ChromeIcon::ChevronDown, chevron_color, dpr);
        painter.save();
        painter.translate(QPointF(twisty_rect.center()) + QPointF(0.5, 0.5));
        if (row.collapsed)
            painter.rotate(-90.0);
        auto target = QRectF(-6.0, -6.0, 12.0, 12.0);
        painter.drawPixmap(target, chevron, QRectF(chevron.rect()));
        painter.restore();
    }

    // 4. Favicon + loading ring + attention dot
    int content_x = twisty_rect.right() + (row.has_children ? 4 : -10);
    auto favicon_rect = QRect(content_x, row_rect.top() + (row_rect.height() - TREE_FAVICON_SIZE) / 2, TREE_FAVICON_SIZE, TREE_FAVICON_SIZE);

    painter.save();
    if (!tab->is_loaded())
        painter.setOpacity(TREE_UNLOADED_OPACITY);
    auto icon = tab->tab_icon();
    if (icon.isNull())
        icon = create_chrome_icon(ChromeIcon::Globe, chrome_palette);
    icon.paint(&painter, favicon_rect);
    painter.restore();

    if (tab->loading_ring_opacity() > 0.01) {
        painter.save();
        painter.setOpacity(tab->loading_ring_opacity());
        auto phase = tab->is_waiting_for_response() ? ChromeStyle::LoadingPhase::WaitingForResponse : ChromeStyle::LoadingPhase::Receiving;
        ChromeStyle::paint_loading_ring(painter, chrome_palette, QPointF(favicon_rect.center()), ChromeStyle::LoadingRingSize::Favicon, phase);
        painter.restore();
    }

    if ((tab->needs_attention() && !is_active) || (row.collapsed && row.has_attention_descendant)) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Theme::accent());
        painter.drawEllipse(QPointF(favicon_rect.right() + 1, favicon_rect.top() + 1), 3.0, 3.0);
    }

    // 5. Trailing controls: close button, audio button, collapsed child count badge
    int right_edge = row_rect.right() - 6;
    bool shows_close = is_active || is_hovered;
    if (shows_close) {
        auto close_rect = close_button_rect_for_row(row_rect);
        bool close_hovered = is_hovered && m_hover.part == RowHitPart::CloseButton;
        if (close_hovered) {
            QPainterPath bg;
            bg.addRoundedRect(QRectF(close_rect), 4, 4);
            painter.fillPath(bg, colors.strip_hover);
        }
        auto close_color = close_hovered ? colors.text : colors.muted;
        auto close_pixmap = create_chrome_icon_pixmap(ChromeIcon::TabClose, close_color, dpr);
        painter.drawPixmap(QRectF(close_rect.center().x() - 6, close_rect.center().y() - 6, 12, 12), close_pixmap, QRectF(close_pixmap.rect()));
        right_edge = close_rect.left() - 4;
    }

    bool has_audio = TreeTabController::tab_has_audio(*tab);
    bool show_audio = has_audio || (row.collapsed && row.has_audio_descendant);
    if (show_audio) {
        auto audio_rect = audio_button_rect_for_row(row_rect, shows_close);
        bool audio_hovered = has_audio && is_hovered && m_hover.part == RowHitPart::AudioButton;
        if (audio_hovered) {
            QPainterPath bg;
            bg.addRoundedRect(QRectF(audio_rect), 4, 4);
            painter.fillPath(bg, colors.strip_hover);
        }
        bool is_muted = tab->is_loaded() && tab->view().page_mute_state() == Web::HTML::MuteState::Muted;
        auto audio_icon = is_muted ? ChromeIcon::VolumeMuted : ChromeIcon::Volume;
        auto audio_pixmap = create_chrome_icon_pixmap(audio_icon, is_active ? colors.text : colors.icon, dpr);
        painter.drawPixmap(QRectF(audio_rect.center().x() - 6, audio_rect.center().y() - 6, 12, 12), audio_pixmap, QRectF(audio_pixmap.rect()));
        right_edge = audio_rect.left() - 4;
    }

    if (row.collapsed && row.descendant_count > 0) {
        auto badge_font = ChromeStyle::chrome_title_font(font());
        badge_font.setPointSizeF(max(8.0, badge_font.pointSizeF() - 1.5));
        badge_font.setWeight(QFont::Medium);
        QFontMetrics badge_metrics(badge_font);
        auto badge_text = QString::number(row.descendant_count);
        int badge_w = max(18, badge_metrics.horizontalAdvance(badge_text) + 10);
        int badge_h = 16;
        QRect badge_rect(right_edge - badge_w, row_rect.top() + (row_rect.height() - badge_h) / 2, badge_w, badge_h);

        QPainterPath pill;
        pill.addRoundedRect(QRectF(badge_rect), badge_h / 2.0, badge_h / 2.0);
        auto pill_bg = row.has_active_descendant ? Theme::accent() : colors.strip_hover;
        painter.fillPath(pill, pill_bg);
        painter.setFont(badge_font);
        painter.setPen(row.has_active_descendant ? Qt::white : colors.muted);
        painter.drawText(badge_rect, Qt::AlignCenter, badge_text);
        right_edge = badge_rect.left() - 5;
    }

    // 6. Tab title
    auto title_font = ChromeStyle::chrome_title_font(font());
    if (is_active)
        title_font.setWeight(QFont::Medium);
    painter.setFont(title_font);

    QColor title_color = is_active ? colors.text : colors.row_title;
    if (!tab->is_loaded())
        title_color.setAlphaF(0.6);
    painter.setPen(title_color);

    int title_left = favicon_rect.right() + 7;
    int title_width = max(0, right_edge - title_left);
    if (title_width > 4) {
        auto title = tab->title().isEmpty() ? QStringLiteral("New Tab") : tab->title();
        QFontMetrics metrics(title_font);
        auto elided = metrics.elidedText(title, Qt::ElideRight, title_width);
        painter.drawText(QRect(title_left, row_rect.top(), title_width, row_rect.height()), Qt::AlignVCenter | Qt::AlignLeft, elided);
    }
}

void TreeTabView::paint_drop_indicator(QPainter& painter, int pinned_height)
{
    if (!m_dragging || !m_drop_row_index.has_value())
        return;

    auto index = *m_drop_row_index;
    if (index < 0 || index >= static_cast<int>(m_rows.size()))
        return;

    auto const& row = m_rows[static_cast<size_t>(index)];
    auto rect = tree_row_rect(index, pinned_height);
    auto accent = Theme::accent();

    if (m_drop_position == TreeDropPosition::Inside) {
        QPainterPath outline;
        outline.addRoundedRect(QRectF(rect).adjusted(1, 1, -1, -1), TREE_ROW_RADIUS, TREE_ROW_RADIUS);
        auto fill = accent;
        fill.setAlphaF(0.14);
        painter.fillPath(outline, fill);
        painter.setPen(QPen(accent, 1.5));
        painter.drawPath(outline);
        return;
    }

    auto visual_depth = min(row.depth + ((m_drop_position == TreeDropPosition::Below && row.has_children && !row.collapsed) ? 1uz : 0uz), TREE_MAX_VISUAL_DEPTH);
    int line_left = rect.left() + 8 + static_cast<int>(visual_depth) * TREE_INDENT_PER_LEVEL;
    int line_y = (m_drop_position == TreeDropPosition::Above) ? rect.top() : (rect.bottom() + 1);

    painter.setPen(QPen(accent, 2.0));
    painter.drawLine(line_left, line_y, rect.right() - 4, line_y);
    painter.setBrush(accent);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(line_left, line_y), 3.0, 3.0);
}

void TreeTabView::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);

    auto chrome_palette = Theme::chrome_palette(*this);
    auto pinned = compute_pinned_layout();
    paint_pinned_region(painter, pinned, chrome_palette);

    auto clip_top = TREE_TOP_PADDING + pinned.total_height;
    painter.save();
    painter.setClipRect(0, clip_top, viewport()->width(), max(0, viewport()->height() - clip_top));

    auto scroll_y = verticalScrollBar()->value();
    auto first_visible = max(0, scroll_y / TREE_ROW_HEIGHT);
    auto last_visible = min(static_cast<int>(m_rows.size()) - 1, (scroll_y + viewport()->height()) / TREE_ROW_HEIGHT + 1);

    for (int i = first_visible; i <= last_visible; ++i) {
        auto rect = tree_row_rect(i, pinned.total_height);
        paint_tree_row(painter, i, rect, chrome_palette);
    }

    paint_drop_indicator(painter, pinned.total_height);
    painter.restore();
}

void TreeTabView::mousePressEvent(QMouseEvent* event)
{
    auto pos = event->position().toPoint();
    auto hit = hit_test(pos);

    if (event->button() == Qt::MiddleButton) {
        if (hit.index >= 0) {
            if (hit.is_pinned) {
                auto pinned = m_controller.pinned_tabs();
                if (hit.index < static_cast<int>(pinned.size()) && pinned[static_cast<size_t>(hit.index)])
                    m_controller.request_close_tab(*pinned[static_cast<size_t>(hit.index)]);
            } else if (hit.index < static_cast<int>(m_rows.size()) && m_rows[static_cast<size_t>(hit.index)].tab) {
                m_controller.request_close_tab(*m_rows[static_cast<size_t>(hit.index)].tab);
            }
        }
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    m_press_pos = pos;
    m_pressed_node_id.clear();
    m_pressed_pinned_tab.clear();
    m_dragging = false;

    if (hit.index < 0) {
        m_controller.clear_selection();
        return;
    }

    if (hit.is_pinned) {
        auto pinned = m_controller.pinned_tabs();
        if (hit.index < static_cast<int>(pinned.size()) && pinned[static_cast<size_t>(hit.index)]) {
            auto* tab = pinned[static_cast<size_t>(hit.index)];
            m_pressed_pinned_tab = tab;
            m_controller.clear_selection();
            m_controller.activate_tab(*tab);
        }
        return;
    }

    auto const& row = m_rows[static_cast<size_t>(hit.index)];
    if (!row.tab)
        return;

    if (hit.part == RowHitPart::Twisty) {
        m_controller.toggle_branch_collapsed(*row.tab);
        return;
    }
    if (hit.part == RowHitPart::CloseButton) {
        m_controller.request_close_tab(*row.tab);
        return;
    }
    if (hit.part == RowHitPart::AudioButton) {
        if (row.tab->is_loaded())
            row.tab->view().toggle_page_mute_state();
        viewport()->update();
        return;
    }

    auto mods = event->modifiers();
    if (mods.testFlag(Qt::ControlModifier) || mods.testFlag(Qt::MetaModifier)) {
        m_controller.toggle_selection(row.node_id);
        return;
    }
    if (mods.testFlag(Qt::ShiftModifier)) {
        m_controller.select_range_to(row.node_id);
        return;
    }

    m_pressed_node_id = row.node_id;
    m_drag_include_subtree = !mods.testFlag(Qt::AltModifier);
    if (!m_controller.model().is_selected(row.node_id) || m_controller.model().selected_count() <= 1)
        m_controller.select_only(row.node_id);
    m_controller.activate_tab(*row.tab);
}

void TreeTabView::update_drop_target(QPoint const& viewport_pos)
{
    auto pinned = compute_pinned_layout();
    auto tree_top = TREE_TOP_PADDING + pinned.total_height;
    if (m_rows.is_empty() || viewport_pos.y() < tree_top) {
        m_drop_row_index.clear();
        m_auto_expand_timer->stop();
        m_auto_expand_node_id.clear();
        return;
    }

    auto virtual_y = viewport_pos.y() - tree_top + verticalScrollBar()->value();
    auto row_index = clamp(virtual_y / TREE_ROW_HEIGHT, 0, static_cast<int>(m_rows.size()) - 1);
    auto rect = tree_row_rect(row_index, pinned.total_height);

    qreal rel_y = rect.height() > 0 ? static_cast<qreal>(viewport_pos.y() - rect.top()) / rect.height() : 0.5;
    TreeDropPosition pos = TreeDropPosition::Inside;
    if (rel_y < 0.25)
        pos = TreeDropPosition::Above;
    else if (rel_y > 0.75)
        pos = TreeDropPosition::Below;

    m_drop_row_index = row_index;
    m_drop_position = pos;

    auto const& target_row = m_rows[static_cast<size_t>(row_index)];
    if (pos == TreeDropPosition::Inside && target_row.collapsed) {
        if (m_auto_expand_node_id != target_row.node_id) {
            m_auto_expand_node_id = target_row.node_id;
            m_auto_expand_timer->start();
        }
    } else {
        m_auto_expand_timer->stop();
        m_auto_expand_node_id.clear();
    }
}

void TreeTabView::clear_drag_state()
{
    m_dragging = false;
    m_pressed_node_id.clear();
    m_pressed_pinned_tab.clear();
    m_drop_row_index.clear();
    m_auto_expand_timer->stop();
    m_auto_expand_node_id.clear();
    unsetCursor();
    viewport()->update();
}

void TreeTabView::mouseMoveEvent(QMouseEvent* event)
{
    auto pos = event->position().toPoint();

    if (m_pressed_node_id.has_value() && (event->buttons() & Qt::LeftButton)) {
        if (!m_dragging && (pos - m_press_pos).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
        }
        if (m_dragging) {
            m_drag_include_subtree = !event->modifiers().testFlag(Qt::AltModifier);
            update_drop_target(pos);
            viewport()->update();
            return;
        }
    }

    auto hit = hit_test(pos);
    if (hit.is_pinned != m_hover.is_pinned || hit.index != m_hover.index || hit.part != m_hover.part) {
        m_hover = hit;
        if (hit.index >= 0) {
            if (hit.is_pinned) {
                auto pinned = m_controller.pinned_tabs();
                if (hit.index < static_cast<int>(pinned.size()) && pinned[static_cast<size_t>(hit.index)])
                    setToolTip(pinned[static_cast<size_t>(hit.index)]->title());
            } else if (hit.index < static_cast<int>(m_rows.size()) && m_rows[static_cast<size_t>(hit.index)].tab) {
                setToolTip(m_rows[static_cast<size_t>(hit.index)].tab->title());
            }
        } else {
            setToolTip({});
        }
        viewport()->update();
    }
}

void TreeTabView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_dragging && m_pressed_node_id.has_value()) {
        auto dragged_id = *m_pressed_node_id;
        auto pos = event->position().toPoint();
        bool dragged_outside = pos.x() < -TREE_DETACH_MARGIN_PX || pos.x() > viewport()->width() + TREE_DETACH_MARGIN_PX;

        if (dragged_outside) {
            auto global_pos = event->globalPosition().toPoint();
            if (auto const* node = m_controller.model().node(dragged_id); node && node->tab) {
                auto* tab = node->tab;
                auto include_subtree = m_drag_include_subtree;
                clear_drag_state();
                if (include_subtree)
                    m_controller.move_branch_to_new_window(*tab, global_pos);
                else if (auto idx = m_controller.window().tab_index(tab); idx >= 0 && m_controller.window().tab_count() > 1)
                    m_controller.window().detach_tab_to_new_window(idx, global_pos);
                return;
            }
        } else if (m_drop_row_index.has_value() && *m_drop_row_index >= 0 && *m_drop_row_index < static_cast<int>(m_rows.size())) {
            auto target_id = m_rows[static_cast<size_t>(*m_drop_row_index)].node_id;
            auto drop_pos = m_drop_position;
            auto include_subtree = m_drag_include_subtree;
            clear_drag_state();
            m_controller.handle_drop(dragged_id, target_id, drop_pos, include_subtree);
            return;
        }
    }

    clear_drag_state();
}

void TreeTabView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    auto hit = hit_test(event->position().toPoint());
    if (hit.index < 0) {
        m_controller.window().open_new_tab();
        return;
    }
    if (!hit.is_pinned && hit.index < static_cast<int>(m_rows.size())) {
        auto const& row = m_rows[static_cast<size_t>(hit.index)];
        if (row.tab && row.has_children)
            m_controller.toggle_branch_collapsed(*row.tab);
    }
}

void TreeTabView::leaveEvent(QEvent* event)
{
    QAbstractScrollArea::leaveEvent(event);
    if (!m_dragging) {
        m_hover = {};
        viewport()->update();
    }
}

void TreeTabView::contextMenuEvent(QContextMenuEvent* event)
{
    auto hit = hit_test(event->pos());
    if (hit.index < 0) {
        m_controller.window().show_window_context_menu(event->globalPos());
        return;
    }

    if (hit.is_pinned) {
        auto pinned = m_controller.pinned_tabs();
        if (hit.index < static_cast<int>(pinned.size()) && pinned[static_cast<size_t>(hit.index)]) {
            if (auto* menu = pinned[static_cast<size_t>(hit.index)]->context_menu())
                execute_context_menu(*menu, event->globalPos());
        }
        return;
    }

    if (hit.index < static_cast<int>(m_rows.size()))
        m_controller.show_context_menu_for_node(m_rows[static_cast<size_t>(hit.index)].node_id, event->globalPos());
}

void TreeTabView::keyPressEvent(QKeyEvent* event)
{
    if (m_rows.is_empty()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    auto* current = m_controller.window().current_tab();
    int current_row = -1;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (m_rows[static_cast<size_t>(i)].tab == current) {
            current_row = i;
            break;
        }
    }

    switch (event->key()) {
    case Qt::Key_Down: {
        int next = min(static_cast<int>(m_rows.size()) - 1, current_row + 1);
        if (next >= 0 && m_rows[static_cast<size_t>(next)].tab)
            m_controller.activate_tab(*m_rows[static_cast<size_t>(next)].tab);
        return;
    }
    case Qt::Key_Up: {
        int prev = current_row <= 0 ? 0 : current_row - 1;
        if (prev >= 0 && m_rows[static_cast<size_t>(prev)].tab)
            m_controller.activate_tab(*m_rows[static_cast<size_t>(prev)].tab);
        return;
    }
    case Qt::Key_Left: {
        if (current_row >= 0) {
            auto const& row = m_rows[static_cast<size_t>(current_row)];
            if (row.tab && row.has_children && !row.collapsed) {
                m_controller.set_branch_collapsed(*row.tab, true);
            } else if (auto const* node = m_controller.model().node(row.node_id); node && node->parent_id.has_value()) {
                if (auto const* parent = m_controller.model().node(*node->parent_id); parent && parent->tab)
                    m_controller.activate_tab(*parent->tab);
            }
        }
        return;
    }
    case Qt::Key_Right: {
        if (current_row >= 0) {
            auto const& row = m_rows[static_cast<size_t>(current_row)];
            if (row.tab && row.has_children && row.collapsed) {
                m_controller.set_branch_collapsed(*row.tab, false);
            } else if (row.has_children && current_row + 1 < static_cast<int>(m_rows.size()) && m_rows[static_cast<size_t>(current_row + 1)].tab) {
                m_controller.activate_tab(*m_rows[static_cast<size_t>(current_row + 1)].tab);
            }
        }
        return;
    }
    case Qt::Key_Tab: {
        if (current)
            m_controller.indent_tab(*current);
        return;
    }
    case Qt::Key_Backtab: {
        if (current)
            m_controller.outdent_tab(*current);
        return;
    }
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
        if (m_controller.model().selected_count() > 1)
            m_controller.close_tabs(m_controller.selected_tabs());
        else if (current)
            m_controller.request_close_tab(*current);
        return;
    }
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
}

void TreeTabView::wheelEvent(QWheelEvent* event)
{
    QAbstractScrollArea::wheelEvent(event);
    viewport()->update();
}

}
