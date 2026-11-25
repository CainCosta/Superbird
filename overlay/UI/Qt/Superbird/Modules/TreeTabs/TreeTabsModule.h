/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabsActions.h>

#include <QPointer>

class QAction;
class QMenu;

namespace Ladybird {
class BrowserWindow;
class Tab;
class TreeTabController;
}

namespace Superbird {

// First Superbird module: wraps TreeTabController and attaches all Tree-Style Tabs UI seams
// (sidebar widget, Tab context menu branch actions, window Tab Layout toggle) and its action bus entries.
class TreeTabsModule final : public SuperbirdModule {
public:
    AK_ALLOC_WITH_KMALLOC;

    static constexpr StringView MODULE_ID = TREE_TABS_MODULE_ID;
    static ModuleInfo module_info();

    // Registers the Tree-Style Tabs actions on the app-wide action bus; they act on the active window.
    static void register_actions(ModuleRegistry&);

    explicit TreeTabsModule(Ladybird::BrowserWindow& window);
    virtual ~TreeTabsModule() override;

    virtual StringView id() const override { return MODULE_ID; }

    Ladybird::TreeTabController& controller() { return *m_controller; }
    Ladybird::TreeTabController const& controller() const { return *m_controller; }

    virtual void on_enabled_changed(bool enabled) override;
    virtual void on_settings_changed() override;
    virtual void on_chrome_style_changed() override;

    virtual void on_pending_opener(Ladybird::Tab* opener) override;
    virtual void on_tab_added(Ladybird::Tab& tab) override;
    virtual void on_tab_removed(Ladybird::Tab& tab) override;
    virtual void on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab) override;
    virtual void on_tab_session_id_assigned(Ladybird::Tab& tab) override;
    virtual void on_tab_pinned_changed(Ladybird::Tab& tab) override;
    virtual void on_active_tab_changed(Ladybird::Tab* tab) override;
    virtual void on_tab_metadata_changed() override;
    virtual void on_tab_moved_externally() override;
    virtual void on_tabs_restored() override;

    virtual void attach_tab_context_menu(Ladybird::Tab& tab, QMenu& menu) override;
    virtual void update_tab_context_menu(Ladybird::Tab& tab) override;

    virtual void attach_window_tab_layout_menu(QMenu& tab_layout_menu) override;
    virtual void update_window_tab_layout_menu() override;

    virtual void append_tab_context(Ladybird::Tab& tab, Vector<String>& labels) override;
    virtual void on_tab_visibility_changed() override;

private:
    struct TabMenuActions {
        QPointer<QAction> new_child_tab;
        QPointer<QAction> branch_separator;
        QPointer<QAction> toggle_branch;
        QPointer<QAction> unload_branch;
        QPointer<QAction> bookmark_branch;
        QPointer<QAction> move_branch_to_new_window;
        QPointer<QAction> close_branch;
    };

    void sync_sidebar_visibility();

    Ladybird::BrowserWindow& m_window;
    NonnullOwnPtr<Ladybird::TreeTabController> m_controller;
    HashMap<Ladybird::Tab*, TabMenuActions> m_tab_menu_actions;
    QPointer<QAction> m_window_layout_separator;
    QPointer<QAction> m_window_tree_tabs_action;
};

}
