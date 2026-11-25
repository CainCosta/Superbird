/*
 * Copyright (c) 2026, Sidelady contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <LibWebView/Settings.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabModel.h>

#include <QObject>
#include <QPoint>
#include <QPointer>

class QTimer;

namespace Ladybird {

class BrowserWindow;
class Tab;
class TreeTabView;

// Coordinates TreeTabModel, TreeTabView, BrowserWindow, and TreeTabStore for one BrowserWindow.
class TreeTabController final : public QObject {
    Q_OBJECT

public:
    AK_ALLOC_WITH_KMALLOC;

    explicit TreeTabController(BrowserWindow& window);
    virtual ~TreeTabController() override;

    TreeTabView* view() const;
    TreeTabModel const& model() const { return m_model; }
    BrowserWindow& window() const { return m_window; }

    bool is_enabled() const;

    // Opener tracking: set immediately before creating a child tab, consumed by on_tab_added().
    void set_pending_opener(Tab* opener);
    void clear_pending_opener();

    // Lifecycle hooks invoked from BrowserWindow / TabWidget seams.
    void on_tab_added(Tab& tab);
    void on_tab_removed(Tab& tab);
    void on_tab_replaced(Tab const& old_tab, Tab& new_tab);
    void on_tab_session_id_assigned(Tab& tab);
    void on_tab_pinned_changed(Tab& tab);
    void on_active_tab_changed(Tab* tab);
    void on_tab_metadata_changed();
    void on_tab_moved_externally();
    void on_settings_changed();
    void on_chrome_style_changed();
    void on_tabs_restored();

    // Tree queries for Tab context menus and view rendering.
    bool has_children(Tab const& tab) const;
    bool is_collapsed(Tab const& tab) const;
    size_t descendant_count(Tab const& tab) const;
    bool has_unloadable_tabs_in_branch(Tab const& tab) const;
    Vector<Tab*> pinned_tabs() const;
    Vector<VisibleTreeRow> visible_rows() const;
    static bool tab_has_audio(Tab const& tab);

    // User actions (invoked from TreeTabView, Tab context menus, and keyboard shortcuts).
    void activate_tab(Tab& tab);
    void toggle_branch_collapsed(Tab& tab);
    void set_branch_collapsed(Tab& tab, bool collapsed);
    void new_child_tab(Tab& parent_tab);
    void request_close_tab(Tab& tab);
    void close_branch(Tab& root_tab);
    void close_tabs(Vector<Tab*> const& tabs);
    void unload_branch(Tab& root_tab);
    void unload_tabs(Vector<Tab*> const& tabs);
    void bookmark_branch(Tab& root_tab);
    void bookmark_tabs(Vector<Tab*> const& tabs, QString const& default_folder_name);
    void move_branch_to_new_window(Tab& root_tab, Optional<QPoint> global_position = {});
    void move_tabs_to_new_window(Vector<Tab*> const& tabs, Optional<QPoint> global_position = {});
    void indent_tab(Tab& tab);
    void outdent_tab(Tab& tab);

    // Selection and Drag & Drop operations.
    void select_only(TreeNodeId node_id);
    void toggle_selection(TreeNodeId node_id);
    void select_range_to(TreeNodeId node_id);
    void clear_selection();
    Vector<Tab*> selected_tabs() const;
    bool handle_drop(TreeNodeId dragged_node_id, TreeNodeId target_node_id, TreeDropPosition position, bool include_subtree);
    void show_context_menu_for_node(TreeNodeId node_id, QPoint global_position);

    void refresh_view();

    // The sidebar's quick filter: while it has text, the tree shows only matching tabs (with their ancestors).
    void set_filter_text(QString const&);
    QString const& filter_text() const { return m_filter_text; }
    void focus_filter();

    // Branch color tags, persisted in TreeTabs.db.
    BranchColor branch_color(Tab const&) const;
    void set_branch_color(Tab&, BranchColor);

    // Tabs this says are hidden (another workspace's, say) don't appear in the tree.
    void set_tab_hidden_predicate(Function<bool(Tab&)> predicate) { m_is_tab_hidden = move(predicate); }

private:
    void sync_flat_tab_order();
    void reconcile_model_with_window();
    void schedule_persist();
    void persist_now();
    bool confirm_close_multiple_tabs(size_t count);
    void close_tab_list_safely(Vector<Tab*> const& tabs_to_close);
    void transfer_subtree_topology(BrowserWindow& target_window, Vector<Tab*> const& moved_tabs, HashMap<Tab const*, Tab*> const& parent_map, HashMap<Tab const*, bool> const& collapsed_map);

    BrowserWindow& m_window;
    TreeTabModel m_model;
    QPointer<TreeTabView> m_view;
    QPointer<Tab> m_pending_opener;
    Function<bool(Tab&)> m_is_tab_hidden;
    QString m_filter_text;
    QTimer* m_persist_timer { nullptr };

    bool m_syncing_flat_order { false };
    bool m_closing_multiple { false };
    bool m_restoring_tabs { false };
};

}
