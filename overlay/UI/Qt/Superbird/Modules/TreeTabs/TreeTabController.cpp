/*
 * Copyright (c) 2026, Sidelady contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashMap.h>
#include <AK/HashTable.h>
#include <AK/ScopeGuard.h>
#include <UI/Qt/Application.h>
#include <UI/Qt/BookmarkStore.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/Menu.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/TabBar.h>
#include <UI/Qt/Superbird/Core/Confirm.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabController.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabFilter.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabStore.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabView.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabsModule.h>

#include <QMenu>
#include <QTimer>

namespace Ladybird {

TreeTabController::TreeTabController(BrowserWindow& window)
    : QObject(&window)
    , m_window(window)
{
    m_view = new TreeTabView(*this);

    m_persist_timer = new QTimer(this);
    m_persist_timer->setSingleShot(true);
    m_persist_timer->setInterval(150);
    connect(m_persist_timer, &QTimer::timeout, this, &TreeTabController::persist_now);

    m_model.set_auto_branch_colors(auto_branch_colors_enabled());
}

TreeTabController::~TreeTabController()
{
    if (m_persist_timer && m_persist_timer->isActive()) {
        m_persist_timer->stop();
        persist_now();
    }
}

TreeTabView* TreeTabController::view() const
{
    return m_view;
}

bool TreeTabController::is_enabled() const
{
    if (!Superbird::ModuleRegistry::the().is_enabled(Superbird::TreeTabsModule::MODULE_ID))
        return false;
    auto const& settings = Application::settings().tab_settings();
    return settings.vertical_tabs_enabled && settings.tree_tabs_enabled;
}

void TreeTabController::set_pending_opener(Tab* opener)
{
    m_pending_opener = opener;
}

void TreeTabController::clear_pending_opener()
{
    m_pending_opener.clear();
}

void TreeTabController::on_tab_added(Tab& tab)
{
    auto opener = m_pending_opener;
    m_pending_opener.clear();

    if (tab.is_pinned()) {
        refresh_view();
        return;
    }

    Optional<TreeNodeId> parent_id;
    if (opener && !opener->is_pinned())
        parent_id = m_model.node_id_for_tab(opener.data());

    auto const& settings = Application::settings().tab_settings();
    auto node_id = m_model.add_tab(&tab, tab.session_tab_id(), parent_id, settings.tree_tabs_child_position);
    if (parent_id.has_value())
        m_model.reveal_node(node_id);

    if (!m_restoring_tabs) {
        sync_flat_tab_order();
        schedule_persist();
    }
    refresh_view();
}

void TreeTabController::on_tab_removed(Tab& tab)
{
    auto node_id = m_model.node_id_for_tab(&tab);
    if (node_id.has_value()) {
        auto close_behavior = m_closing_multiple
            ? WebView::TreeTabCloseBehavior::AttachToGrandparent
            : Application::settings().tab_settings().tree_tabs_close_behavior;
        m_model.remove_node(*node_id, close_behavior);
        if (!m_closing_multiple)
            sync_flat_tab_order();
        schedule_persist();
    }
    if (auto session_tab_id = tab.session_tab_id(); session_tab_id.has_value())
        Application::tree_tab_store(m_window.is_private()).remove_tab(*session_tab_id);
    refresh_view();
}

void TreeTabController::on_tab_replaced(Tab const& old_tab, Tab& new_tab)
{
    m_model.replace_tab(&old_tab, &new_tab);
    if (new_tab.session_tab_id().has_value())
        m_model.set_session_tab_id(&new_tab, new_tab.session_tab_id());
    refresh_view();
}

void TreeTabController::on_tab_session_id_assigned(Tab& tab)
{
    if (tab.is_pinned())
        return;
    m_model.set_session_tab_id(&tab, tab.session_tab_id());
    schedule_persist();
}

void TreeTabController::on_tab_pinned_changed(Tab& tab)
{
    if (tab.is_pinned()) {
        if (auto node_id = m_model.node_id_for_tab(&tab); node_id.has_value())
            m_model.remove_node(*node_id, WebView::TreeTabCloseBehavior::AttachToGrandparent);
    } else {
        if (!m_model.node_id_for_tab(&tab).has_value())
            m_model.add_tab(&tab, tab.session_tab_id());
    }
    sync_flat_tab_order();
    schedule_persist();
    refresh_view();
}

void TreeTabController::on_active_tab_changed(Tab* tab)
{
    if (tab && !tab->is_pinned()) {
        if (auto node_id = m_model.node_id_for_tab(tab); node_id.has_value()) {
            if (m_model.reveal_node(*node_id))
                schedule_persist();
        }
    }
    refresh_view();
    if (m_view && tab)
        m_view->ensure_tab_visible(tab);
}

void TreeTabController::on_tab_metadata_changed()
{
    if (m_view && m_view->isVisible())
        m_view->update();
}

void TreeTabController::on_tab_moved_externally()
{
    if (m_syncing_flat_order || m_restoring_tabs)
        return;
    reconcile_model_with_window();
    refresh_view();
}

void TreeTabController::on_settings_changed()
{
    m_model.set_auto_branch_colors(auto_branch_colors_enabled());
    reconcile_model_with_window();
    if (is_enabled())
        sync_flat_tab_order();
    refresh_view();
}

void TreeTabController::on_chrome_style_changed()
{
    if (m_view)
        m_view->update_chrome_style();
}

void TreeTabController::on_tabs_restored()
{
    m_restoring_tabs = true;
    ScopeGuard clear_restoring = [&] { m_restoring_tabs = false; };

    reconcile_model_with_window();
    for (int i = m_window.pinned_tab_count(); i < m_window.tab_count(); ++i) {
        if (auto* tab = m_window.tab_at(i))
            m_model.set_session_tab_id(tab, tab->session_tab_id());
    }

    auto persisted = Application::tree_tab_store(m_window.is_private()).load_all();
    m_model.apply_persisted_state(persisted);

    if (auto* current = m_window.current_tab(); current && !current->is_pinned()) {
        if (auto node_id = m_model.node_id_for_tab(current); node_id.has_value())
            m_model.reveal_node(*node_id);
    }

    sync_flat_tab_order();
    refresh_view();
}

bool TreeTabController::has_children(Tab const& tab) const
{
    auto node_id = m_model.node_id_for_tab(&tab);
    if (!node_id.has_value())
        return false;
    auto const* entry = m_model.node(*node_id);
    return entry && !entry->children.is_empty();
}

bool TreeTabController::is_collapsed(Tab const& tab) const
{
    auto node_id = m_model.node_id_for_tab(&tab);
    if (!node_id.has_value())
        return false;
    auto const* entry = m_model.node(*node_id);
    return entry && !entry->children.is_empty() && entry->collapsed;
}

size_t TreeTabController::descendant_count(Tab const& tab) const
{
    auto node_id = m_model.node_id_for_tab(&tab);
    if (!node_id.has_value())
        return 0;
    return m_model.descendant_count(*node_id);
}

bool TreeTabController::has_unloadable_tabs_in_branch(Tab const& tab) const
{
    auto node_id = m_model.node_id_for_tab(&tab);
    if (!node_id.has_value())
        return false;
    auto* current = m_window.current_tab();
    for (auto* candidate : m_model.subtree_tabs(*node_id)) {
        if (candidate && candidate != current && !candidate->is_pinned() && candidate->is_loaded())
            return true;
    }
    return false;
}

Vector<Tab*> TreeTabController::pinned_tabs() const
{
    Vector<Tab*> tabs;
    auto pinned_count = m_window.pinned_tab_count();
    tabs.ensure_capacity(pinned_count);
    for (int i = 0; i < pinned_count; ++i) {
        if (auto* tab = m_window.tab_at(i))
            tabs.append(tab);
    }
    return tabs;
}

bool TreeTabController::tab_has_audio(Tab const& tab)
{
    if (!tab.is_loaded())
        return false;
    return tab.view().tab_indicator_visible(WebView::TabIndicator::PlayingAudio)
        || tab.view().page_mute_state() == Web::HTML::MuteState::Muted;
}

Vector<VisibleTreeRow> TreeTabController::visible_rows() const
{
    Function<bool(Tab const&)> is_hidden;
    if (m_is_tab_hidden) {
        is_hidden = [this](Tab const& tab) { return m_is_tab_hidden(const_cast<Tab&>(tab)); };
    }

    Vector<VisibleTreeRow> rows;
    if (auto query = m_filter_text.trimmed(); !query.isEmpty()) {
        // NB: Qt folds case for every script, so "É" finds "é"; the matcher then compares the folded bytes.
        auto query_utf8 = query.toCaseFolded().toUtf8();
        std::string_view query_view { query_utf8.constData(), static_cast<size_t>(query_utf8.size()) };
        rows = m_model.filtered_rows(
            [&](Tab const& tab) {
                auto title = tab.title().toCaseFolded().toUtf8();
                auto url = QString::fromUtf8(tab.url().to_byte_string().characters()).toCaseFolded().toUtf8();
                return Superbird::TreeTabs::tab_matches_filter({ title.constData(), static_cast<size_t>(title.size()) }, { url.constData(), static_cast<size_t>(url.size()) }, query_view);
            },
            is_hidden);
    } else {
        rows = m_model.visible_rows(
            m_window.current_tab(),
            [](Tab const& tab) { return tab_has_audio(tab); },
            [](Tab const& tab) { return tab.needs_attention(); },
            is_hidden);
    }
    // Tabs another module keeps out of view (another workspace's) leave the tree too.
    if (m_is_tab_hidden)
        rows.remove_all_matching([&](VisibleTreeRow const& row) { return row.tab && m_is_tab_hidden(*row.tab); });
    return rows;
}

void TreeTabController::activate_tab(Tab& tab)
{
    auto index = m_window.tab_index(&tab);
    if (index >= 0)
        m_window.activate_tab(index);
}

void TreeTabController::toggle_branch_collapsed(Tab& tab)
{
    auto node_id = m_model.node_id_for_tab(&tab);
    if (!node_id.has_value())
        return;

    auto auto_collapse = Application::settings().tab_settings().tree_tabs_auto_collapse_siblings;
    if (!m_model.toggle_collapsed(*node_id, auto_collapse))
        return;

    // If collapsing hid the active tab, focus the collapsed parent so the active tab stays visible.
    if (is_collapsed(tab)) {
        if (auto* current = m_window.current_tab(); current && current != &tab) {
            if (auto active_id = m_model.node_id_for_tab(current); active_id.has_value() && m_model.is_ancestor(*node_id, *active_id))
                activate_tab(tab);
        }
    }

    schedule_persist();
    refresh_view();
}

void TreeTabController::set_branch_collapsed(Tab& tab, bool collapsed)
{
    auto node_id = m_model.node_id_for_tab(&tab);
    if (!node_id.has_value())
        return;

    auto auto_collapse = Application::settings().tab_settings().tree_tabs_auto_collapse_siblings;
    if (!m_model.set_collapsed(*node_id, collapsed, auto_collapse))
        return;

    if (collapsed) {
        if (auto* current = m_window.current_tab(); current && current != &tab) {
            if (auto active_id = m_model.node_id_for_tab(current); active_id.has_value() && m_model.is_ancestor(*node_id, *active_id))
                activate_tab(tab);
        }
    }

    schedule_persist();
    refresh_view();
}

void TreeTabController::new_child_tab(Tab& parent_tab)
{
    if (auto node_id = m_model.node_id_for_tab(&parent_tab); node_id.has_value())
        m_model.set_collapsed(*node_id, false, false);

    set_pending_opener(&parent_tab);
    auto& new_tab = m_window.new_tab_from_url(Application::settings().new_tab_page_url(), Web::HTML::ActivateTab::Yes, BrowserWindow::TabLocation::after_tab(parent_tab));
    new_tab.set_url_is_hidden(true);
    new_tab.focus_location_editor();
}


void TreeTabController::request_close_tab(Tab& tab)
{
    auto node_id = m_model.node_id_for_tab(&tab);
    auto close_behavior = Application::settings().tab_settings().tree_tabs_close_behavior;

    if (is_enabled() && node_id.has_value() && close_behavior == WebView::TreeTabCloseBehavior::CloseBranch && has_children(tab)) {
        close_branch(tab);
        return;
    }

    auto index = m_window.tab_index(&tab);
    if (index >= 0)
        m_window.request_to_close_tab(index);
}

void TreeTabController::close_branch(Tab& root_tab)
{
    auto node_id = m_model.node_id_for_tab(&root_tab);
    if (!node_id.has_value()) {
        if (auto index = m_window.tab_index(&root_tab); index >= 0)
            m_window.request_to_close_tab(index);
        return;
    }

    auto tabs = m_model.subtree_tabs(*node_id);
    if (tabs.size() > 1 && is_collapsed(root_tab)) {
        // The branch is folded, so the tabs it holds aren't in view: ask first. The branch is looked up again once the
        // user answers, as tabs may have come and gone meanwhile.
        Superbird::ask_to_confirm(m_window, qformatted("Close {} tabs?", tabs.size()), "They're in this collapsed branch, out of view.", "Close Tabs",
            [this, root = QPointer(&root_tab)] {
                if (!root)
                    return;
                if (auto id = m_model.node_id_for_tab(root.data()); id.has_value())
                    close_tab_list_safely(m_model.subtree_tabs(*id));
            });
        return;
    }

    close_tab_list_safely(tabs);
}

void TreeTabController::close_tabs(Vector<Tab*> const& tabs)
{
    if (tabs.is_empty())
        return;
    close_tab_list_safely(tabs);
}

void TreeTabController::close_tab_list_safely(Vector<Tab*> const& tabs_to_close)
{
    HashTable<Tab const*> closing_set;
    Vector<QPointer<Tab>> ordered_targets;
    for (auto* tab : tabs_to_close) {
        if (!tab || closing_set.contains(tab))
            continue;
        closing_set.set(tab);
        ordered_targets.append(tab);
    }
    if (ordered_targets.is_empty())
        return;

    // Activate a surviving tab first so closing the active tab doesn't needlessly spin up a deferred tab that is also closing.
    if (auto* current = m_window.current_tab(); current && closing_set.contains(current)) {
        for (int i = 0; i < m_window.tab_count(); ++i) {
            if (auto* candidate = m_window.tab_at(i); candidate && !closing_set.contains(candidate)) {
                m_window.activate_tab(i);
                break;
            }
        }
    }

    m_closing_multiple = true;
    ScopeGuard finish_closing = [&] {
        m_closing_multiple = false;
        sync_flat_tab_order();
        schedule_persist();
        refresh_view();
    };

    // Close leaves before parents so tree order stays coherent even if a beforeunload dialog cancels one close.
    for (size_t i = ordered_targets.size(); i > 0; --i) {
        auto tab = ordered_targets[i - 1];
        if (!tab)
            continue;
        if (auto index = m_window.tab_index(tab.data()); index >= 0)
            m_window.request_to_close_tab(index);
    }
}

void TreeTabController::unload_branch(Tab& root_tab)
{
    auto node_id = m_model.node_id_for_tab(&root_tab);
    if (!node_id.has_value())
        return;
    unload_tabs(m_model.subtree_tabs(*node_id));
}

void TreeTabController::unload_tabs(Vector<Tab*> const& tabs)
{
    Vector<QPointer<Tab>> targets;
    auto* current = m_window.current_tab();
    for (auto* tab : tabs) {
        if (tab && tab != current && !tab->is_pinned() && tab->is_loaded())
            targets.append(tab);
    }
    for (auto const& tab : targets) {
        if (tab && tab->is_loaded() && tab.data() != m_window.current_tab())
            m_window.discard_tab(*tab);
    }
    refresh_view();
}

void TreeTabController::bookmark_branch(Tab& root_tab)
{
    auto node_id = m_model.node_id_for_tab(&root_tab);
    if (!node_id.has_value())
        return;
    auto folder_name = root_tab.title().isEmpty() ? QStringLiteral("Tab Branch") : root_tab.title();
    bookmark_tabs(m_model.subtree_tabs(*node_id), folder_name);
}

void TreeTabController::bookmark_tabs(Vector<Tab*> const& tabs, QString const& default_folder_name)
{
    if (tabs.is_empty())
        return;

    auto& store = Application::bookmark_store();
    auto folder_title = ak_string_from_qstring(default_folder_name.isEmpty() ? QStringLiteral("Tab Branch") : default_folder_name);
    auto folder_id = store.add_folder(folder_title);
    for (auto* tab : tabs) {
        if (!tab)
            continue;
        auto title = tab->title().isEmpty() ? Optional<String> {} : Optional<String> { ak_string_from_qstring(tab->title()) };
        store.add_bookmark(tab->url(), title, {}, folder_id);
    }
}

void TreeTabController::move_branch_to_new_window(Tab& root_tab, Optional<QPoint> global_position)
{
    auto node_id = m_model.node_id_for_tab(&root_tab);
    if (!node_id.has_value()) {
        if (auto index = m_window.tab_index(&root_tab); index >= 0 && m_window.tab_count() > 1) {
            auto pos = global_position.value_or(m_window.mapToGlobal(QPoint(m_window.width() / 4, m_window.height() / 4)));
            m_window.detach_tab_to_new_window(index, pos);
        }
        return;
    }
    move_tabs_to_new_window(m_model.subtree_tabs(*node_id), global_position);
}

void TreeTabController::move_tabs_to_new_window(Vector<Tab*> const& tabs, Optional<QPoint> global_position)
{
    if (tabs.is_empty() || m_window.tab_count() <= 1)
        return;

    // Snapshot relative parent-child relationships & collapsed state before detaching from this window.
    HashTable<Tab const*> moving_set;
    Vector<QPointer<Tab>> moving_tabs;
    for (auto* tab : tabs) {
        if (tab && !moving_set.contains(tab)) {
            moving_set.set(tab);
            moving_tabs.append(tab);
        }
    }
    if (moving_tabs.size() >= static_cast<size_t>(m_window.tab_count()))
        return;

    HashMap<Tab const*, Tab*> parent_map;
    HashMap<Tab const*, bool> collapsed_map;
    for (auto const& tab : moving_tabs) {
        if (!tab)
            continue;
        auto node_id = m_model.node_id_for_tab(tab.data());
        if (!node_id.has_value())
            continue;
        auto const* entry = m_model.node(*node_id);
        if (!entry)
            continue;
        collapsed_map.set(tab.data(), entry->collapsed);
        if (entry->parent_id.has_value()) {
            if (auto const* parent = m_model.node(*entry->parent_id); parent && parent->tab && moving_set.contains(parent->tab))
                parent_map.set(tab.data(), parent->tab);
        }
    }

    auto first_tab = moving_tabs[0];
    if (!first_tab)
        return;
    auto first_index = m_window.tab_index(first_tab.data());
    if (first_index < 0)
        return;

    auto pos = global_position.value_or(m_window.mapToGlobal(QPoint(m_window.width() / 4, m_window.height() / 4)));
    auto& target_window = m_window.detach_tab_to_new_window(first_index, pos);

    Vector<Tab*> moved_ptrs;
    moved_ptrs.append(first_tab.data());
    for (size_t i = 1; i < moving_tabs.size(); ++i) {
        auto tab = moving_tabs[i];
        if (!tab)
            continue;
        auto idx = m_window.tab_index(tab.data());
        if (idx < 0)
            continue;
        m_window.move_tab_to_window(idx, target_window, target_window.tab_count());
        moved_ptrs.append(tab.data());
    }

    transfer_subtree_topology(target_window, moved_ptrs, parent_map, collapsed_map);
    sync_flat_tab_order();
    schedule_persist();
    refresh_view();
}

void TreeTabController::transfer_subtree_topology(
    BrowserWindow& target_window,
    Vector<Tab*> const& moved_tabs,
    HashMap<Tab const*, Tab*> const& parent_map,
    HashMap<Tab const*, bool> const& collapsed_map)
{
    auto* target_controller = target_window.superbird().tree_tab_controller();
    if (!target_controller)
        return;
    for (auto* tab : moved_tabs) {
        auto node_id = target_controller->m_model.node_id_for_tab(tab);
        if (!node_id.has_value())
            continue;
        if (auto parent_tab = parent_map.get(tab); parent_tab.has_value()) {
            if (auto parent_node_id = target_controller->m_model.node_id_for_tab(*parent_tab); parent_node_id.has_value()) {
                auto const* parent_node = target_controller->m_model.node(*parent_node_id);
                target_controller->m_model.move_node(*node_id, *parent_node_id, parent_node ? parent_node->children.size() : 0, true);
            }
        }
    }
    for (auto* tab : moved_tabs) {
        auto node_id = target_controller->m_model.node_id_for_tab(tab);
        if (!node_id.has_value())
            continue;
        if (auto collapsed = collapsed_map.get(tab); collapsed.has_value() && *collapsed)
            target_controller->m_model.set_collapsed(*node_id, true, false);
    }
    target_controller->sync_flat_tab_order();
    target_controller->schedule_persist();
    target_controller->refresh_view();
}

void TreeTabController::indent_tab(Tab& tab)
{
    auto node_id = m_model.node_id_for_tab(&tab);
    if (!node_id.has_value())
        return;
    if (m_model.indent_node(*node_id)) {
        sync_flat_tab_order();
        schedule_persist();
        refresh_view();
    }
}

void TreeTabController::outdent_tab(Tab& tab)
{
    auto node_id = m_model.node_id_for_tab(&tab);
    if (!node_id.has_value())
        return;
    if (m_model.outdent_node(*node_id)) {
        sync_flat_tab_order();
        schedule_persist();
        refresh_view();
    }
}

void TreeTabController::select_only(TreeNodeId node_id)
{
    m_model.select_only(node_id);
    refresh_view();
}

void TreeTabController::toggle_selection(TreeNodeId node_id)
{
    m_model.toggle_selection(node_id);
    refresh_view();
}

void TreeTabController::select_range_to(TreeNodeId node_id)
{
    auto rows = visible_rows();
    auto anchor = m_model.selection_anchor();
    if (!anchor.has_value()) {
        if (auto* current = m_window.current_tab())
            anchor = m_model.node_id_for_tab(current);
    }
    if (!anchor.has_value()) {
        m_model.select_only(node_id);
    } else {
        m_model.select_range(*anchor, node_id, rows);
    }
    refresh_view();
}

void TreeTabController::clear_selection()
{
    m_model.clear_selection();
    refresh_view();
}

Vector<Tab*> TreeTabController::selected_tabs() const
{
    Vector<Tab*> tabs;
    for (auto id : m_model.selected_nodes_in_order()) {
        if (auto const* entry = m_model.node(id); entry && entry->tab)
            tabs.append(entry->tab);
    }
    return tabs;
}

bool TreeTabController::handle_drop(TreeNodeId dragged_node_id, TreeNodeId target_node_id, TreeDropPosition position, bool include_subtree)
{
    bool moved = false;

    // If dragging a tab that is part of a multi-selection, move all top-level selected nodes together.
    if (m_model.selected_count() > 1 && m_model.is_selected(dragged_node_id)) {
        auto selected = m_model.selected_nodes_in_order();
        Vector<TreeNodeId> top_selected;
        for (auto id : selected) {
            bool ancestor_selected = false;
            for (auto other : selected) {
                if (other != id && m_model.is_ancestor(other, id)) {
                    ancestor_selected = true;
                    break;
                }
            }
            if (!ancestor_selected && id != target_node_id)
                top_selected.append(id);
        }

        auto current_target = target_node_id;
        auto current_position = position;
        for (auto id : top_selected) {
            if (m_model.drop_node(id, current_target, current_position, include_subtree)) {
                moved = true;
                current_target = id;
                current_position = TreeDropPosition::Below;
            }
        }
    } else {
        moved = m_model.drop_node(dragged_node_id, target_node_id, position, include_subtree);
    }

    if (moved) {
        sync_flat_tab_order();
        schedule_persist();
        refresh_view();
    }
    return moved;
}

void TreeTabController::show_context_menu_for_node(TreeNodeId node_id, QPoint global_position)
{
    auto const* entry = m_model.node(node_id);
    if (!entry || !entry->tab)
        return;

    // Multi-selection context menu when right-clicking within a multi-selection of >= 2 tabs.
    if (m_model.selected_count() > 1 && m_model.is_selected(node_id)) {
        auto tabs = selected_tabs();
        if (tabs.size() > 1) {
            QMenu menu(&m_window);
            auto* unload_action = menu.addAction(qformatted("&Unload {} Selected Tabs", tabs.size()));
            connect(unload_action, &QAction::triggered, this, [this, tabs] { unload_tabs(tabs); });

            auto* bookmark_action = menu.addAction(qformatted("&Bookmark {} Selected Tabs…", tabs.size()));
            connect(bookmark_action, &QAction::triggered, this, [this, tabs] { bookmark_tabs(tabs, QStringLiteral("Selected Tabs")); });

            auto* move_window_action = menu.addAction(qformatted("Move {} Selected Tabs to New &Window", tabs.size()));
            move_window_action->setEnabled(static_cast<int>(tabs.size()) < m_window.tab_count());
            connect(move_window_action, &QAction::triggered, this, [this, tabs, global_position] {
                move_tabs_to_new_window(tabs, global_position);
            });

            menu.addSeparator();
            auto* close_action = menu.addAction(qformatted("&Close {} Selected Tabs", tabs.size()));
            connect(close_action, &QAction::triggered, this, [this, tabs] { close_tabs(tabs); });

            execute_context_menu(menu, global_position);
            return;
        }
    }

    if (auto* context_menu = entry->tab->context_menu())
        execute_context_menu(*context_menu, global_position);
}

void TreeTabController::sync_flat_tab_order()
{
    if (m_syncing_flat_order)
        return;

    m_syncing_flat_order = true;
    ScopeGuard reset_sync = [&] { m_syncing_flat_order = false; };

    reconcile_model_with_window();

    auto pinned_count = m_window.pinned_tab_count();
    auto desired = m_model.linearized_tabs();
    for (size_t i = 0; i < desired.size(); ++i) {
        auto target_index = pinned_count + static_cast<int>(i);
        auto current_index = m_window.tab_index(desired[i]);
        if (current_index >= pinned_count && current_index != target_index && target_index < m_window.tab_count())
            m_window.move_tab(current_index, target_index);
    }
}

void TreeTabController::reconcile_model_with_window()
{
    Vector<Tab*> unpinned;
    auto pinned_count = m_window.pinned_tab_count();
    auto total = m_window.tab_count();
    if (total > pinned_count) {
        unpinned.ensure_capacity(static_cast<size_t>(total - pinned_count));
        for (int i = pinned_count; i < total; ++i) {
            if (auto* tab = m_window.tab_at(i))
                unpinned.append(tab);
        }
    }
    m_model.reconcile_with_unpinned_tabs(unpinned);
}

void TreeTabController::schedule_persist()
{
    if (m_window.is_private() == WebView::IsPrivate::Yes)
        return;
    if (m_persist_timer && !m_persist_timer->isActive())
        m_persist_timer->start();
}

void TreeTabController::persist_now()
{
    if (m_window.is_private() == WebView::IsPrivate::Yes)
        return;
    auto window_id = m_window.session_window_id();
    if (!window_id.has_value())
        return;
    auto snapshot = m_model.snapshot_persisted_state();
    Application::tree_tab_store(m_window.is_private()).sync_window_nodes(*window_id, snapshot);
}

void TreeTabController::refresh_view()
{
    if (m_view)
        m_view->rebuild_rows();
}

void TreeTabController::set_filter_text(QString const& text)
{
    if (m_filter_text == text)
        return;
    m_filter_text = text;
    refresh_view();
}

void TreeTabController::focus_filter()
{
    if (m_view)
        m_view->focus_filter();
}

bool TreeTabController::auto_branch_colors_enabled() const
{
    return Superbird::WindowHost::module_option_is_on(Superbird::TreeTabsModule::MODULE_ID, Superbird::TREE_TABS_AUTO_BRANCH_COLORS_OPTION);
}

BranchColor TreeTabController::branch_color(Tab const& tab) const
{
    auto node_id = m_model.node_id_for_tab(&tab);
    return node_id.has_value() ? m_model.branch_color(*node_id) : BranchColor::None;
}

void TreeTabController::set_branch_color(Tab& tab, BranchColor color)
{
    auto node_id = m_model.node_id_for_tab(&tab);
    if (!node_id.has_value() || !m_model.set_branch_color(*node_id, color))
        return;
    schedule_persist();
    refresh_view();
}

}
