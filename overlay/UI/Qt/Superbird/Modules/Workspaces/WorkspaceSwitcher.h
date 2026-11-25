/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceModel.h>

#include <QIcon>
#include <QWidget>

class QMenu;

namespace Superbird {

// Draws a workspace's badge: its icon glyph (or the first letter of its name) in a rounded square, filled with its
// tint (or the accent when active and untinted).
QIcon workspace_badge_icon(Workspaces::Workspace const&, bool active, QPalette const&, int size = 16);

// The switcher at the bottom of the vertical tab sidebar: a badge per workspace and a "+" to add one. Narrower than
// all the badges (the collapsed rail), it shows the active badge only, which opens a menu of all of them.
class WorkspaceSwitcher final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit WorkspaceSwitcher(QWidget* parent = nullptr);

    void set_workspaces(std::vector<Workspaces::Workspace>, Workspaces::WorkspaceId active);

    Function<void(Workspaces::WorkspaceId)> on_activate;
    Function<void()> on_create;
    // Fills the context menu for a workspace (rename, icon, color, move, delete).
    Function<void(QMenu&, Workspaces::WorkspaceId)> on_build_menu;
    // Fills the menu the collapsed switcher shows.
    Function<void(QMenu&)> on_build_overview_menu;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

private:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void mouseMoveEvent(QMouseEvent*) override;
    virtual void leaveEvent(QEvent*) override;
    virtual void mousePressEvent(QMouseEvent*) override;
    virtual void contextMenuEvent(QContextMenuEvent*) override;
    virtual bool event(QEvent*) override;

    bool is_compact() const;
    // -1 for none, workspaces().size() for the "+" button.
    int slot_at(QPoint) const;
    QRect slot_rect(int slot) const;

    std::vector<Workspaces::Workspace> m_workspaces;
    Workspaces::WorkspaceId m_active { 0 };
    int m_hovered_slot { -1 };
};

}
