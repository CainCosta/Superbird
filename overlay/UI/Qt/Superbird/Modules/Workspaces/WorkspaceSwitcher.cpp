/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/ChromeStyle.h>
#include <UI/Qt/Icon.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceSwitcher.h>
#include <UI/Qt/Theme.h>

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

namespace Superbird {

static constexpr int SLOT_SIZE = 26;
static constexpr int SLOT_GAP = 4;
static constexpr int BADGE_SIZE = 18;
static constexpr int HORIZONTAL_PADDING = 10;
static constexpr int VERTICAL_PADDING = 6;

static QString badge_text(Workspaces::Workspace const& workspace)
{
    if (!workspace.icon.empty())
        return QString::fromUtf8(workspace.icon.data(), static_cast<qsizetype>(workspace.icon.size()));
    auto name = QString::fromUtf8(workspace.name.data(), static_cast<qsizetype>(workspace.name.size()));
    return name.isEmpty() ? QString("?") : name.left(1).toUpper();
}

static QColor badge_fill(Workspaces::Workspace const& workspace, bool active, QPalette const& palette)
{
    if (workspace.tint.has_value()) {
        auto color = QColor::fromRgb(*workspace.tint);
        if (!active)
            color.setAlphaF(Ladybird::Theme::is_dark(palette) ? 0.45 : 0.35);
        return color;
    }
    if (active)
        return Ladybird::Theme::accent();
    return Ladybird::Theme::chrome(palette).strip_hover;
}

static void paint_badge(QPainter& painter, QRectF const& rect, Workspaces::Workspace const& workspace, bool active, QPalette const& palette)
{
    auto fill = badge_fill(workspace, active, palette);
    QPainterPath path;
    path.addRoundedRect(rect, rect.width() * 0.3, rect.width() * 0.3);
    painter.fillPath(path, fill);

    auto font = painter.font();
    font.setPixelSize(qRound(rect.height() * 0.6));
    font.setBold(true);
    painter.setFont(font);
    // White on a tint or the accent, otherwise the chrome's text color.
    auto on_fill = active || workspace.tint.has_value() ? QColor(Qt::white) : Ladybird::Theme::chrome(palette).text;
    painter.setPen(on_fill);
    painter.drawText(rect, Qt::AlignCenter, badge_text(workspace));
}

QIcon workspace_badge_icon(Workspaces::Workspace const& workspace, bool active, QPalette const& palette, int size)
{
    QIcon icon;
    for (int scale = 1; scale <= 3; ++scale) {
        QPixmap pixmap(size * scale, size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        paint_badge(painter, QRectF(1, 1, size - 2, size - 2), workspace, active, palette);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

WorkspaceSwitcher::WorkspaceSwitcher(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("SuperbirdWorkspaceSwitcher");
    setAccessibleName("Workspaces");
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void WorkspaceSwitcher::set_workspaces(std::vector<Workspaces::Workspace> workspaces, Workspaces::WorkspaceId active)
{
    m_workspaces = std::move(workspaces);
    m_active = active;
    m_hovered_slot = -1;
    updateGeometry();
    update();
}

QSize WorkspaceSwitcher::sizeHint() const
{
    auto slot_count = static_cast<int>(m_workspaces.size()) + 1;
    return { (2 * HORIZONTAL_PADDING) + (slot_count * SLOT_SIZE) + ((slot_count - 1) * SLOT_GAP), SLOT_SIZE + (2 * VERTICAL_PADDING) };
}

QSize WorkspaceSwitcher::minimumSizeHint() const
{
    return { SLOT_SIZE + 8, SLOT_SIZE + (2 * VERTICAL_PADDING) };
}

bool WorkspaceSwitcher::is_compact() const
{
    return width() < sizeHint().width();
}

QRect WorkspaceSwitcher::slot_rect(int slot) const
{
    if (is_compact())
        return slot == 0 ? QRect((width() - SLOT_SIZE) / 2, VERTICAL_PADDING, SLOT_SIZE, SLOT_SIZE) : QRect();
    return { HORIZONTAL_PADDING + (slot * (SLOT_SIZE + SLOT_GAP)), VERTICAL_PADDING, SLOT_SIZE, SLOT_SIZE };
}

int WorkspaceSwitcher::slot_at(QPoint point) const
{
    auto slot_count = is_compact() ? 1 : static_cast<int>(m_workspaces.size()) + 1;
    for (int slot = 0; slot < slot_count; ++slot) {
        if (slot_rect(slot).contains(point))
            return slot;
    }
    return -1;
}

void WorkspaceSwitcher::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    auto palette = Ladybird::Theme::chrome_palette(*this);
    auto const& chrome = Ladybird::Theme::chrome(palette);

    auto paint_slot_background = [&](int slot) {
        if (slot != m_hovered_slot)
            return;
        QPainterPath path;
        path.addRoundedRect(QRectF(slot_rect(slot)), 6, 6);
        painter.fillPath(path, chrome.strip_hover);
    };
    auto badge_rect = [&](int slot) {
        auto rect = QRectF(slot_rect(slot));
        auto inset = (SLOT_SIZE - BADGE_SIZE) / 2.0;
        return rect.adjusted(inset, inset, -inset, -inset);
    };

    if (is_compact()) {
        auto active = std::find_if(m_workspaces.begin(), m_workspaces.end(), [&](auto const& workspace) { return workspace.id == m_active; });
        if (active != m_workspaces.end()) {
            paint_slot_background(0);
            paint_badge(painter, badge_rect(0), *active, true, palette);
        }
        return;
    }

    for (size_t index = 0; index < m_workspaces.size(); ++index) {
        auto slot = static_cast<int>(index);
        auto const& workspace = m_workspaces[index];
        auto active = workspace.id == m_active;
        paint_slot_background(slot);
        paint_badge(painter, badge_rect(slot), workspace, active, palette);
        if (active) {
            // A short accent bar under the active workspace, like the selected tab's.
            auto rect = slot_rect(slot);
            painter.fillRect(QRectF(rect.center().x() - 5, rect.bottom() + 1, 10, 2), Ladybird::Theme::accent());
        }
    }

    auto plus_slot = static_cast<int>(m_workspaces.size());
    paint_slot_background(plus_slot);
    auto plus_icon = Ladybird::create_chrome_icon(Ladybird::ChromeIcon::NewTab, palette);
    auto plus_rect = slot_rect(plus_slot);
    plus_icon.paint(&painter, QRect(plus_rect.center().x() - 7, plus_rect.center().y() - 7, 14, 14));
}

void WorkspaceSwitcher::mouseMoveEvent(QMouseEvent* event)
{
    auto slot = slot_at(event->position().toPoint());
    if (slot != m_hovered_slot) {
        m_hovered_slot = slot;
        update();
    }
}

void WorkspaceSwitcher::leaveEvent(QEvent*)
{
    m_hovered_slot = -1;
    update();
}

void WorkspaceSwitcher::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    auto slot = slot_at(event->position().toPoint());
    if (slot < 0)
        return;
    if (is_compact()) {
        QMenu menu(this);
        if (on_build_overview_menu)
            on_build_overview_menu(menu);
        menu.exec(mapToGlobal(slot_rect(0).topLeft()));
        return;
    }
    if (slot == static_cast<int>(m_workspaces.size())) {
        if (on_create)
            on_create();
        return;
    }
    if (on_activate)
        on_activate(m_workspaces[static_cast<size_t>(slot)].id);
}

void WorkspaceSwitcher::contextMenuEvent(QContextMenuEvent* event)
{
    auto slot = slot_at(event->pos());
    QMenu menu(this);
    if (is_compact() || slot < 0 || slot >= static_cast<int>(m_workspaces.size())) {
        if (on_build_overview_menu)
            on_build_overview_menu(menu);
    } else if (on_build_menu) {
        on_build_menu(menu, m_workspaces[static_cast<size_t>(slot)].id);
    }
    if (!menu.isEmpty())
        menu.exec(event->globalPos());
}

bool WorkspaceSwitcher::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        auto slot = slot_at(help->pos());
        QString text;
        if (is_compact() && slot == 0)
            text = "Workspaces";
        else if (slot >= 0 && slot < static_cast<int>(m_workspaces.size()))
            text = QString::fromUtf8(m_workspaces[static_cast<size_t>(slot)].name.c_str());
        else if (slot == static_cast<int>(m_workspaces.size()))
            text = "New Workspace";
        if (text.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), text, this);
        return true;
    }
    return QWidget::event(event);
}

}
