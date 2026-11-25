/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceModel.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacesActions.h>

#include <QPointer>

class QAction;
class QMenu;
class QObject;
class QShortcut;
class QTimer;

namespace Ladybird {
class BrowserWindow;
class Tab;
}

namespace Superbird {

class ModuleRegistry;
class WorkspacePicker;
class WorkspaceSwitcher;

// Workspaces: named sets of a window's tabs. Only the active workspace's tabs show, in the stock tab bar (horizontal
// or vertical) and in Tree-Style Tabs alike; pinned tabs show everywhere. A switcher sits at the bottom of the
// vertical sidebar (or as a badge in the address field with horizontal tabs). Persisted in Workspaces.db.
class WorkspacesModule final : public SuperbirdModule {
public:
    AK_ALLOC_WITH_KMALLOC;

    static constexpr StringView MODULE_ID = WORKSPACES_MODULE_ID;
    static ModuleInfo module_info();
    static void register_actions(ModuleRegistry&);

    explicit WorkspacesModule(Ladybird::BrowserWindow&);
    virtual ~WorkspacesModule() override;

    virtual StringView id() const override { return MODULE_ID; }

    virtual void on_enabled_changed(bool enabled) override;
    virtual void on_settings_changed() override;
    virtual void on_chrome_style_changed() override;
    virtual void on_tab_added(Ladybird::Tab&) override;
    virtual void on_tab_removed(Ladybird::Tab&) override;
    virtual void on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab) override;
    virtual void on_tab_session_id_assigned(Ladybird::Tab&) override;
    virtual void on_active_tab_changed(Ladybird::Tab*) override;
    virtual void on_tabs_restored() override;
    virtual void attach_tab_context_menu(Ladybird::Tab&, QMenu&) override;
    virtual void update_tab_context_menu(Ladybird::Tab&) override;
    virtual void append_tab_context(Ladybird::Tab&, Vector<String>&) override;
    virtual bool is_tab_hidden(Ladybird::Tab&) const override;

    bool can_run(WorkspacesCommand, size_t slot) const;
    void run(WorkspacesCommand, size_t slot);

    // Retitles the bus's "Switch to Workspace" actions for this window's workspaces.
    void publish_action_titles();

private:
    bool is_enabled() const;
    void install();
    void uninstall();
    void load_restored_state();
    Workspaces::TabKey key_for(Ladybird::Tab&);
    std::vector<Workspaces::TabKey> strip_order();
    Ladybird::Tab* tab_for_key(Workspaces::TabKey);

    void switch_to(Workspaces::WorkspaceId);
    // Opens the picker of presets and custom workspaces. What it makes is switched to, or handed to `on_created`.
    void create_workspace_interactively(Function<void(Workspaces::WorkspaceId)> on_created = {});
    void edit_workspace_interactively(Workspaces::WorkspaceId);
    WorkspacePicker& picker();
    void place_picker();
    void delete_workspace(Workspaces::WorkspaceId);
    void delete_workspace_now(Workspaces::WorkspaceId);
    Vector<Ladybird::Tab*> tab_with_branch(Ladybird::Tab&) const;
    void move_tab_to(Ladybird::Tab&, Workspaces::WorkspaceId);
    void fill_workspace_menu(QMenu&, Workspaces::WorkspaceId);
    void fill_overview_menu(QMenu&);

    // After any change: re-apply visibility, refresh the switcher, and persist soon.
    void changed();
    void update_switcher();
    void update_pill_actions();
    void schedule_persist();
    void persist_now();

    Ladybird::BrowserWindow& m_window;
    Workspaces::WorkspaceModel m_model;
    HashMap<Ladybird::Tab*, Workspaces::TabKey> m_keys;
    Workspaces::TabKey m_next_transient_key { -1 };
    bool m_loaded { false };
    bool m_switching { false };
    // Set while the current tab closes, so the window doesn't follow its replacement into another workspace.
    bool m_current_tab_closing { false };

    QPointer<WorkspaceSwitcher> m_switcher;
    QPointer<WorkspacePicker> m_picker;
    HashMap<Ladybird::Tab*, QPointer<QAction>> m_pill_actions;
    HashMap<Ladybird::Tab*, QPointer<QMenu>> m_tab_menus;
    Vector<QPointer<QShortcut>> m_shortcuts;
    QPointer<QTimer> m_persist_timer;
    QPointer<QObject> m_activation_watcher;
};

}
