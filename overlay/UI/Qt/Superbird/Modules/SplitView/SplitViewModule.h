/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitLayout.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitViewActions.h>

#include <QPoint>
#include <QPointer>

#include <optional>

class QAction;
class QMenu;

namespace Ladybird {
class BrowserWindow;
class Tab;
}

namespace Superbird {

class ModuleRegistry;
class SplitViewHost;

// Split View: shows two to four tabs tiled in the window (side by side, stacked, or a grid). The split group lives on
// while you visit other tabs, and comes back when you pick one of its tabs again. Closing a tiled tab collapses its
// pane; with one pane left the window is back to a single page.
class SplitViewModule final : public SuperbirdModule {
public:
    AK_ALLOC_WITH_KMALLOC;

    static constexpr StringView MODULE_ID = SPLIT_VIEW_MODULE_ID;
    static ModuleInfo module_info();
    static void register_actions(ModuleRegistry&);

    explicit SplitViewModule(Ladybird::BrowserWindow&);
    virtual ~SplitViewModule() override;

    virtual StringView id() const override { return MODULE_ID; }

    virtual void on_enabled_changed(bool enabled) override;
    virtual void on_chrome_style_changed() override;
    virtual void on_tab_removed(Ladybird::Tab&) override;
    virtual void on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab) override;
    virtual void on_active_tab_changed(Ladybird::Tab*) override;
    virtual void on_tab_metadata_changed() override;
    virtual void on_tab_visibility_changed() override;
    virtual void attach_tab_context_menu(Ladybird::Tab&, QMenu&) override;
    virtual void update_tab_context_menu(Ladybird::Tab&) override;

    bool can_run(SplitCommand) const;
    void run(SplitCommand);

    // A tab dragged out of the tab strip and dropped near an edge of this window's page splits the page with it.
    bool drop_tab_at(Ladybird::Tab&, QPoint global_position);

private:
    bool is_enabled() const;
    Split::PaneId pane_for(Ladybird::Tab&);
    Ladybird::Tab* tab_for(Split::PaneId) const;
    Ladybird::Tab* next_visible_tab_after(Ladybird::Tab&) const;
    bool is_shown() const { return m_host != nullptr; }

    void split_with(Ladybird::Tab& first, Ladybird::Tab& second, Split::Orientation);
    void split_grid();
    void add_to_split(Ladybird::Tab&, Split::Orientation);
    void remove_from_split(Split::PaneId);
    void show_split();
    void hide_split();
    void unsplit();
    void relayout();
    void focus_pane(Split::PaneId);

    Ladybird::BrowserWindow& m_window;
    std::optional<Split::SplitLayout> m_layout;
    QPointer<SplitViewHost> m_host;
    HashMap<Ladybird::Tab*, Split::PaneId> m_pane_ids;
    Split::PaneId m_next_pane_id { 1 };
    bool m_changing { false };
    HashMap<Ladybird::Tab*, QPointer<QAction>> m_split_with_current_actions;
    HashMap<Ladybird::Tab*, QPointer<QAction>> m_add_to_split_actions;
};

}
