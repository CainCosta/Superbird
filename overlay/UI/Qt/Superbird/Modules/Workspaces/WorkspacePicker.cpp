/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Function.h>
#include <UI/Qt/ChromeStyle.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceGlyphs.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacePicker.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacePresets.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceSwitcher.h>
#include <UI/Qt/Theme.h>

#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace Superbird {

static constexpr int CARD_WIDTH = 336;
static constexpr int CARD_PADDING = 16;
static constexpr int PRESET_COLUMNS = 3;
static constexpr int PRESET_TILE_HEIGHT = 78;
static constexpr int PRESET_BADGE_SIZE = 30;
static constexpr int ICON_COLUMNS = 8;
static constexpr int ICON_CHOICE_SIZE = 32;
static constexpr int TINT_CHOICE_SIZE = 26;
static constexpr int PREVIEW_SIZE = 36;
static constexpr int LETTER_ICON_ID = 0;
static constexpr int ACCENT_TINT_ID = 0;

static QString to_qstring(std::string_view string)
{
    return QString::fromUtf8(string.data(), static_cast<qsizetype>(string.size()));
}

static std::string to_std_string(QString const& string)
{
    auto utf8 = string.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

// An icon painted at each of the device pixel ratios the chrome's icons come in.
static QIcon paint_icon(int size, Function<void(QPainter&, QRectF const&)> const& paint)
{
    QIcon icon;
    for (int scale = 1; scale <= 3; ++scale) {
        QPixmap pixmap(size * scale, size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        paint(painter, QRectF(0, 0, size, size));
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

static Workspaces::Workspace sample_workspace(std::string_view name, std::string icon, std::optional<uint32_t> tint)
{
    return { .id = 0, .name = std::string(name), .icon = std::move(icon), .tint = tint };
}

WorkspacePicker::WorkspacePicker(QWidget* parent)
    : Popover(parent)
{
    setAccessibleName("Workspaces");
    // NB: The message popover's object names pick up its style sheet, so the picker matches the chrome's own popovers.
    card().setObjectName("LadybirdMessagePopover");
    card().setFixedWidth(CARD_WIDTH);

    auto* layout = new QVBoxLayout(&card());
    layout->setContentsMargins(CARD_PADDING, CARD_PADDING, CARD_PADDING, CARD_PADDING);
    layout->setSpacing(0);

    build_presets_page();
    build_editor_page();
    layout->addWidget(m_presets_page);
    layout->addWidget(m_editor_page);
    m_editor_page->hide();
}

void WorkspacePicker::build_presets_page()
{
    m_presets_page = new QWidget(&card());
    auto* layout = new QVBoxLayout(m_presets_page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* title = new QLabel("New Workspace", m_presets_page);
    title->setObjectName("LadybirdMessagePopoverTitle");
    layout->addWidget(title);
    auto* body = new QLabel("Start from a preset, or make your own.", m_presets_page);
    body->setObjectName("LadybirdMessagePopoverBody");
    layout->addWidget(body);
    layout->addSpacing(10);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(8);
    auto add_tile = [&](QString const& label, int index) {
        auto* tile = new QToolButton(m_presets_page);
        tile->setObjectName("SuperbirdWorkspacePresetTile");
        tile->setText(label);
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tile->setIconSize(QSize(PRESET_BADGE_SIZE, PRESET_BADGE_SIZE));
        tile->setFixedHeight(PRESET_TILE_HEIGHT);
        tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        tile->setCursor(Qt::PointingHandCursor);
        grid->addWidget(tile, index / PRESET_COLUMNS, index % PRESET_COLUMNS);
        return tile;
    };

    int index = 0;
    for (auto const& preset : Workspaces::PRESETS) {
        auto* tile = add_tile(to_qstring(preset.name), index++);
        QObject::connect(tile, &QToolButton::clicked, this, [this, &preset] {
            auto accept = AK::move(on_accept);
            close();
            if (accept)
                accept({ .name = std::string(preset.name), .icon = Workspaces::icon_for_glyph(preset.glyph), .tint = preset.tint });
            on_accept = AK::move(accept);
        });
        m_preset_tiles.append(tile);
    }
    m_custom_tile = add_tile("Custom\xE2\x80\xA6", index);
    m_custom_tile->setToolTip("Choose a name, an icon and a color");
    QObject::connect(m_custom_tile, &QToolButton::clicked, this, [this] {
        open_editor(EditorMode::Create, sample_workspace({}, Workspaces::icon_for_glyph(Workspaces::Glyph::Star), m_custom_tint));
    });
    layout->addLayout(grid);
}

void WorkspacePicker::build_editor_page()
{
    m_editor_page = new QWidget(&card());
    auto* layout = new QVBoxLayout(m_editor_page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_editor_title = new QLabel(m_editor_page);
    m_editor_title->setObjectName("LadybirdMessagePopoverTitle");
    layout->addWidget(m_editor_title);
    layout->addSpacing(12);

    auto* name_row = new QHBoxLayout;
    name_row->setSpacing(10);
    m_preview = new QLabel(m_editor_page);
    m_preview->setFixedSize(PREVIEW_SIZE, PREVIEW_SIZE);
    m_preview->setAlignment(Qt::AlignCenter);
    name_row->addWidget(m_preview);
    m_name_field = new QLineEdit(m_editor_page);
    m_name_field->setObjectName("SuperbirdWorkspaceName");
    m_name_field->setPlaceholderText("Workspace name");
    m_name_field->setAccessibleName("Workspace name");
    m_name_field->setMaxLength(static_cast<int>(Workspaces::MAXIMUM_WORKSPACE_NAME_LENGTH));
    QObject::connect(m_name_field, &QLineEdit::textChanged, this, [this] {
        m_accept_button->setEnabled(!m_name_field->text().trimmed().isEmpty());
        refresh_editor_icons();
        refresh_preview();
    });
    QObject::connect(m_name_field, &QLineEdit::returnPressed, this, [this] { accept_editor(); });
    name_row->addWidget(m_name_field, 1);
    layout->addLayout(name_row);
    layout->addSpacing(14);

    auto add_section = [&](char const* text) {
        auto* label = new QLabel(text, m_editor_page);
        label->setObjectName("SuperbirdWorkspacePickerSection");
        layout->addWidget(label);
        layout->addSpacing(6);
    };

    add_section("Icon");
    m_icon_buttons = new QButtonGroup(this);
    m_icon_buttons->setExclusive(true);
    auto* icon_grid = new QGridLayout;
    icon_grid->setContentsMargins(0, 0, 0, 0);
    icon_grid->setSpacing(4);
    auto add_icon_choice = [&](int id, QString const& tooltip) {
        auto* button = new QToolButton(m_editor_page);
        button->setObjectName("SuperbirdWorkspaceIconChoice");
        button->setCheckable(true);
        button->setToolTip(tooltip);
        button->setAccessibleName(tooltip);
        button->setFixedSize(ICON_CHOICE_SIZE, ICON_CHOICE_SIZE);
        button->setIconSize(QSize(18, 18));
        m_icon_buttons->addButton(button, id);
        icon_grid->addWidget(button, id / ICON_COLUMNS, id % ICON_COLUMNS);
    };
    add_icon_choice(LETTER_ICON_ID, "First Letter of the Name");
    for (size_t index = 0; index < Workspaces::GLYPHS.size(); ++index)
        add_icon_choice(static_cast<int>(index) + 1, to_qstring(Workspaces::GLYPHS[index].label));
    QObject::connect(m_icon_buttons, &QButtonGroup::idClicked, this, [this](int id) {
        m_icon = id == LETTER_ICON_ID ? std::string {} : Workspaces::icon_for_glyph(Workspaces::GLYPHS[static_cast<size_t>(id - 1)].glyph);
        refresh_preview();
    });
    layout->addLayout(icon_grid);
    layout->addSpacing(14);

    add_section("Color");
    m_tint_buttons = new QButtonGroup(this);
    m_tint_buttons->setExclusive(true);
    auto* tint_row = new QHBoxLayout;
    tint_row->setContentsMargins(0, 0, 0, 0);
    tint_row->setSpacing(6);
    auto add_tint_choice = [&](int id, QString const& tooltip) {
        auto* button = new QToolButton(m_editor_page);
        button->setObjectName("SuperbirdWorkspaceTintChoice");
        button->setCheckable(true);
        button->setToolTip(tooltip);
        button->setAccessibleName(tooltip);
        button->setFixedSize(TINT_CHOICE_SIZE, TINT_CHOICE_SIZE);
        button->setIconSize(QSize(16, 16));
        m_tint_buttons->addButton(button, id);
        tint_row->addWidget(button);
    };
    add_tint_choice(ACCENT_TINT_ID, "Accent Color");
    for (size_t index = 0; index < Workspaces::TINTS.size(); ++index)
        add_tint_choice(static_cast<int>(index) + 1, to_qstring(Workspaces::TINTS[index].name));
    tint_row->addStretch(1);
    QObject::connect(m_tint_buttons, &QButtonGroup::idClicked, this, [this](int id) {
        m_tint = id == ACCENT_TINT_ID ? std::optional<uint32_t> {} : Workspaces::TINTS[static_cast<size_t>(id - 1)].rgb;
        refresh_preview();
    });
    layout->addLayout(tint_row);
    layout->addSpacing(18);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    m_back_button = new QPushButton(m_editor_page);
    m_back_button->setObjectName("LadybirdMessagePopoverDismissButton");
    // NB: The platform style lays out a push button by a frame of its own rather than by the button.
    m_back_button->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    m_back_button->setFocusPolicy(Qt::NoFocus);
    QObject::connect(m_back_button, &QPushButton::clicked, this, [this] {
        if (m_editor_mode == EditorMode::Create)
            show_page(Page::Presets);
        else
            close();
    });
    buttons->addWidget(m_back_button);
    buttons->addStretch(1);
    m_accept_button = new QPushButton(m_editor_page);
    m_accept_button->setObjectName("LadybirdMessagePopoverAcceptButton");
    m_accept_button->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    m_accept_button->setDefault(true);
    QObject::connect(m_accept_button, &QPushButton::clicked, this, [this] { accept_editor(); });
    buttons->addWidget(m_accept_button);
    layout->addLayout(buttons);
}

void WorkspacePicker::show_presets(std::vector<std::string> existing_names, uint32_t custom_tint)
{
    m_existing_names = std::move(existing_names);
    m_custom_tint = custom_tint;
    refresh_presets();
    show_page(Page::Presets);
}

void WorkspacePicker::show_editor(Workspaces::Workspace const& workspace)
{
    open_editor(EditorMode::Edit, workspace);
}

void WorkspacePicker::open_editor(EditorMode mode, Workspaces::Workspace const& workspace)
{
    m_editor_mode = mode;
    m_icon = workspace.icon;
    m_tint = workspace.tint;
    m_editor_title->setText(mode == EditorMode::Create ? "Custom Workspace" : "Edit Workspace");
    m_back_button->setText(mode == EditorMode::Create ? "Back" : "Cancel");
    m_accept_button->setText(mode == EditorMode::Create ? "Create" : "Save");
    {
        // NB: Setting the text refreshes the icons and the preview, which read the choices above.
        QSignalBlocker blocker(m_name_field);
        m_name_field->setText(to_qstring(workspace.name));
    }
    m_accept_button->setEnabled(!m_name_field->text().trimmed().isEmpty());

    // An icon kept from before the glyphs (an emoji, say) stays until another is picked, so nothing is checked for it.
    auto check_only = [](QButtonGroup& group, int id) {
        group.setExclusive(false);
        for (auto* button : group.buttons())
            button->setChecked(group.id(button) == id);
        group.setExclusive(true);
    };
    int icon_id = -1;
    if (m_icon.empty()) {
        icon_id = LETTER_ICON_ID;
    } else if (auto glyph = Workspaces::glyph_from_icon(m_icon); glyph.has_value()) {
        for (size_t index = 0; index < Workspaces::GLYPHS.size(); ++index) {
            if (Workspaces::GLYPHS[index].glyph == *glyph)
                icon_id = static_cast<int>(index) + 1;
        }
    }
    check_only(*m_icon_buttons, icon_id);
    int tint_id = ACCENT_TINT_ID;
    if (m_tint.has_value()) {
        tint_id = -1;
        for (size_t index = 0; index < Workspaces::TINTS.size(); ++index) {
            if (Workspaces::TINTS[index].rgb == *m_tint)
                tint_id = static_cast<int>(index) + 1;
        }
    }
    check_only(*m_tint_buttons, tint_id);

    refresh_editor_icons();
    refresh_preview();
    show_page(Page::Editor);
    m_name_field->setFocus(Qt::OtherFocusReason);
    m_name_field->selectAll();
}

void WorkspacePicker::show_page(Page page)
{
    m_page = page;
    m_presets_page->setVisible(page == Page::Presets);
    m_editor_page->setVisible(page == Page::Editor);
    card().adjustSize();
    adjustSize();
    if (on_size_changed)
        on_size_changed();
    if (page == Page::Editor)
        m_name_field->setFocus(Qt::OtherFocusReason);
}

void WorkspacePicker::showEvent(QShowEvent* event)
{
    Popover::showEvent(event);
    if (m_page == Page::Editor)
        m_name_field->setFocus(Qt::OtherFocusReason);
}

void WorkspacePicker::update_chrome_style(QPalette const& palette)
{
    setPalette(palette);
    auto const& panel = Ladybird::Theme::panel();
    auto color = [](QColor const& value) { return Ladybird::ChromeStyle::style_sheet_color(value); };
    auto extra = QString(R"(
QToolButton#SuperbirdWorkspacePresetTile {
    color: %1;
    background: %2;
    border: 0;
    border-radius: 10px;
    padding: 10px 4px 8px 4px;
    font-size: 12px;
}
QToolButton#SuperbirdWorkspacePresetTile:hover,
QToolButton#SuperbirdWorkspacePresetTile:pressed {
    background: %3;
}
QToolButton#SuperbirdWorkspacePresetTile:disabled {
    color: %4;
}
QToolButton#SuperbirdWorkspaceIconChoice {
    background: transparent;
    border: 0;
    border-radius: 8px;
}
QToolButton#SuperbirdWorkspaceTintChoice {
    background: transparent;
    border: 0;
    border-radius: 13px;
}
QToolButton#SuperbirdWorkspaceIconChoice:hover,
QToolButton#SuperbirdWorkspaceTintChoice:hover {
    background: %2;
}
QToolButton#SuperbirdWorkspaceIconChoice:checked {
    background: %2;
    border: 2px solid %5;
}
QToolButton#SuperbirdWorkspaceTintChoice:checked {
    border: 2px solid %5;
}
QLineEdit#SuperbirdWorkspaceName {
    color: %1;
    background: %6;
    border: 1px solid %7;
    border-radius: 7px;
    padding: 5px 8px;
}
QLineEdit#SuperbirdWorkspaceName:focus {
    border-color: %8;
}
QLabel#SuperbirdWorkspacePickerSection {
    color: %4;
    font-size: 11px;
    font-weight: 600;
}
)")
                     .arg(color(panel.text), color(panel.control), color(panel.pressed), color(panel.muted), color(Ladybird::Theme::primary()),
                         color(panel.page), color(panel.separator), color(Ladybird::Theme::focus_edge()));
    setStyleSheet(Ladybird::ChromeStyle::message_popover_style_sheet(palette) + extra);

    // The tint swatches.
    for (auto* button : m_tint_buttons->buttons()) {
        auto id = m_tint_buttons->id(button);
        auto fill = id == ACCENT_TINT_ID ? Ladybird::Theme::accent() : QColor::fromRgb(Workspaces::TINTS[static_cast<size_t>(id - 1)].rgb);
        button->setIcon(paint_icon(16, [&](QPainter& painter, QRectF const& rect) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawEllipse(rect.adjusted(1, 1, -1, -1));
            if (id == ACCENT_TINT_ID) {
                // The accent follows the system, which a ring inside the swatch hints at.
                painter.setPen(QPen(QColor(255, 255, 255, 200), 1.5));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(rect.adjusted(5, 5, -5, -5));
            }
        }));
    }
    refresh_presets();
    refresh_editor_icons();
    refresh_preview();
}

void WorkspacePicker::refresh_presets()
{
    auto chrome_palette = palette();
    for (size_t index = 0; index < m_preset_tiles.size(); ++index) {
        auto const& preset = Workspaces::PRESETS[index];
        auto* tile = m_preset_tiles[index];
        tile->setIcon(workspace_badge_icon(sample_workspace(preset.name, Workspaces::icon_for_glyph(preset.glyph), preset.tint), true, chrome_palette, PRESET_BADGE_SIZE));
        auto taken = Workspaces::preset_is_taken(preset, m_existing_names);
        tile->setEnabled(!taken);
        tile->setToolTip(taken ? QString("You already have a \xE2\x80\x9C%1\xE2\x80\x9D workspace").arg(to_qstring(preset.name)) : QString());
    }

    // The custom tile: a dashed outline of a badge, with a plus in it.
    auto muted = Ladybird::Theme::panel().muted;
    m_custom_tile->setIcon(paint_icon(PRESET_BADGE_SIZE, [&](QPainter& painter, QRectF const& rect) {
        auto badge = rect.adjusted(1.5, 1.5, -1.5, -1.5);
        QPen outline(muted, 1.5, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
        outline.setDashPattern({ 2.5, 2.5 });
        painter.setPen(outline);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(badge, badge.width() * 0.3, badge.width() * 0.3);
        painter.setPen(QPen(muted, 1.75, Qt::SolidLine, Qt::RoundCap));
        auto center = rect.center();
        auto arm = rect.width() * 0.18;
        painter.drawLine(QPointF(center.x() - arm, center.y()), QPointF(center.x() + arm, center.y()));
        painter.drawLine(QPointF(center.x(), center.y() - arm), QPointF(center.x(), center.y() + arm));
    }));
}

void WorkspacePicker::refresh_editor_icons()
{
    auto body = Ladybird::Theme::panel().body;
    auto name = m_name_field->text().trimmed();
    auto letter = name.isEmpty() ? QString("A") : name.left(1).toUpper();
    for (auto* button : m_icon_buttons->buttons()) {
        auto id = m_icon_buttons->id(button);
        button->setIcon(paint_icon(18, [&](QPainter& painter, QRectF const& rect) {
            if (id == LETTER_ICON_ID) {
                auto font = painter.font();
                font.setPixelSize(13);
                font.setBold(true);
                painter.setFont(font);
                painter.setPen(body);
                painter.drawText(rect, Qt::AlignCenter, letter);
                return;
            }
            paint_workspace_glyph(painter, rect, Workspaces::GLYPHS[static_cast<size_t>(id - 1)].glyph, body);
        }));
    }
}

Workspaces::Workspace WorkspacePicker::edited_workspace() const
{
    auto name = to_std_string(m_name_field->text().trimmed());
    return sample_workspace(name.empty() ? std::string_view("?") : std::string_view(name), m_icon, m_tint);
}

void WorkspacePicker::refresh_preview()
{
    auto icon = workspace_badge_icon(edited_workspace(), true, palette(), PREVIEW_SIZE);
    m_preview->setPixmap(icon.pixmap(QSize(PREVIEW_SIZE, PREVIEW_SIZE), devicePixelRatioF()));
}

void WorkspacePicker::accept_editor()
{
    auto name = m_name_field->text().trimmed();
    if (name.isEmpty())
        return;
    Result result { .name = to_std_string(name), .icon = m_icon, .tint = m_tint };
    // NB: Closing may lead to the picker being reused, so the callback is held across it.
    auto accept = AK::move(on_accept);
    close();
    if (accept)
        accept(std::move(result));
    on_accept = AK::move(accept);
}

}
