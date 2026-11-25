/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Application.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabController.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabView.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabsModule.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/TabBar.h>

#include <QAction>
#include <QMenu>

namespace Superbird {

ModuleInfo TreeTabsModule::module_info()
{
    return {
        .id = MODULE_ID,
        .name = "Tree-Style Tabs"sv,
        .description = "Organize vertical sidebar tabs into collapsible parent/child trees with drag-and-drop nesting and branch actions."sv,
        .enabled_by_default = true,
    };
}

TreeTabsModule::TreeTabsModule(Ladybird::BrowserWindow& window)
    : m_window(window)
    , m_controller(make<Ladybird::TreeTabController>(window))
{
    auto& tabs = m_window.tab_widget();
    tabs.set_tree_tabs_widget(m_controller->view());
    tabs.on_tree_tabs_rail_changed = [this](bool collapsed_rail) {
        if (auto* tree_view = m_controller->view())
            tree_view->set_collapsed_rail(collapsed_rail);
    };
    sync_sidebar_visibility();
}

TreeTabsModule::~TreeTabsModule() = default;

void TreeTabsModule::sync_sidebar_visibility()
{
    m_window.tab_widget().set_tree_tabs_enabled(m_controller->is_enabled());
}

void TreeTabsModule::on_enabled_changed(bool)
{
    sync_sidebar_visibility();
    m_controller->on_settings_changed();
    update_window_tab_layout_menu();
}

void TreeTabsModule::on_settings_changed()
{
    sync_sidebar_visibility();
    m_controller->on_settings_changed();
    update_window_tab_layout_menu();
}

void TreeTabsModule::on_chrome_style_changed()
{
    m_controller->on_chrome_style_changed();
}

void TreeTabsModule::on_pending_opener(Ladybird::Tab* opener)
{
    m_controller->set_pending_opener(opener);
}

void TreeTabsModule::on_tab_added(Ladybird::Tab& tab)
{
    m_controller->on_tab_added(tab);
}

void TreeTabsModule::on_tab_removed(Ladybird::Tab& tab)
{
    m_tab_menu_actions.remove(&tab);
    m_controller->on_tab_removed(tab);
}

void TreeTabsModule::on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab)
{
    m_tab_menu_actions.remove(const_cast<Ladybird::Tab*>(&old_tab));
    m_controller->on_tab_replaced(old_tab, new_tab);
}

void TreeTabsModule::on_tab_session_id_assigned(Ladybird::Tab& tab)
{
    m_controller->on_tab_session_id_assigned(tab);
}

void TreeTabsModule::on_tab_pinned_changed(Ladybird::Tab& tab)
{
    m_controller->on_tab_pinned_changed(tab);
}

void TreeTabsModule::on_active_tab_changed(Ladybird::Tab* tab)
{
    m_controller->on_active_tab_changed(tab);
}

void TreeTabsModule::on_tab_metadata_changed()
{
    m_controller->on_tab_metadata_changed();
}

void TreeTabsModule::on_tab_moved_externally()
{
    m_controller->on_tab_moved_externally();
}

void TreeTabsModule::on_tabs_restored()
{
    m_controller->on_tabs_restored();
}

void TreeTabsModule::attach_tab_context_menu(Ladybird::Tab& tab, QMenu& menu)
{
    auto* new_child_tab_action = new QAction("New &Child Tab", &tab);
    QObject::connect(new_child_tab_action, &QAction::triggered, &tab, [this, tab_ptr = QPointer<Ladybird::Tab>(&tab)]() {
        if (tab_ptr)
            m_controller->new_child_tab(*tab_ptr);
    });

    auto* toggle_branch_action = new QAction(&tab);
    QObject::connect(toggle_branch_action, &QAction::triggered, &tab, [this, tab_ptr = QPointer<Ladybird::Tab>(&tab)]() {
        if (tab_ptr)
            m_controller->toggle_branch_collapsed(*tab_ptr);
    });

    auto* unload_branch_action = new QAction("&Unload Branch", &tab);
    QObject::connect(unload_branch_action, &QAction::triggered, &tab, [this, tab_ptr = QPointer<Ladybird::Tab>(&tab)]() {
        if (tab_ptr)
            m_controller->unload_branch(*tab_ptr);
    });

    auto* bookmark_branch_action = new QAction("&Bookmark Branch…", &tab);
    QObject::connect(bookmark_branch_action, &QAction::triggered, &tab, [this, tab_ptr = QPointer<Ladybird::Tab>(&tab)]() {
        if (tab_ptr)
            m_controller->bookmark_branch(*tab_ptr);
    });

    auto* move_branch_to_new_window_action = new QAction("Move Branch to New Window", &tab);
    QObject::connect(move_branch_to_new_window_action, &QAction::triggered, &tab, [this, tab_ptr = QPointer<Ladybird::Tab>(&tab)]() {
        if (tab_ptr)
            m_controller->move_branch_to_new_window(*tab_ptr);
    });

    auto* close_branch_action = new QAction("Close B&ranch", &tab);
    QObject::connect(close_branch_action, &QAction::triggered, &tab, [this, tab_ptr = QPointer<Ladybird::Tab>(&tab)]() {
        if (tab_ptr)
            m_controller->close_branch(*tab_ptr);
    });

    auto* branch_separator = menu.addSeparator();
    menu.addAction(new_child_tab_action);
    menu.addAction(toggle_branch_action);
    menu.addAction(unload_branch_action);
    menu.addAction(bookmark_branch_action);
    menu.addAction(move_branch_to_new_window_action);
    menu.addAction(close_branch_action);

    m_tab_menu_actions.set(&tab, TabMenuActions {
        .new_child_tab = new_child_tab_action,
        .branch_separator = branch_separator,
        .toggle_branch = toggle_branch_action,
        .unload_branch = unload_branch_action,
        .bookmark_branch = bookmark_branch_action,
        .move_branch_to_new_window = move_branch_to_new_window_action,
        .close_branch = close_branch_action,
    });

    QObject::connect(&tab, &QObject::destroyed, m_controller.ptr(), [this, raw_tab = &tab]() {
        m_tab_menu_actions.remove(raw_tab);
    });
}

void TreeTabsModule::update_tab_context_menu(Ladybird::Tab& tab)
{
    auto entry = m_tab_menu_actions.get(&tab);
    if (!entry.has_value())
        return;

    auto const& actions = *entry;
    auto show_tree_actions = m_controller->is_enabled() && !tab.is_pinned();
    auto has_branch = show_tree_actions && m_controller->has_children(tab);

    if (actions.branch_separator)
        actions.branch_separator->setVisible(show_tree_actions);
    if (actions.new_child_tab)
        actions.new_child_tab->setVisible(show_tree_actions);
    if (actions.toggle_branch) {
        actions.toggle_branch->setVisible(has_branch);
        if (has_branch)
            actions.toggle_branch->setText(m_controller->is_collapsed(tab) ? "E&xpand Branch" : "Co&llapse Branch");
    }
    if (actions.unload_branch) {
        actions.unload_branch->setVisible(has_branch);
        actions.unload_branch->setEnabled(has_branch && m_controller->has_unloadable_tabs_in_branch(tab));
    }
    if (actions.bookmark_branch)
        actions.bookmark_branch->setVisible(has_branch);
    if (actions.move_branch_to_new_window) {
        actions.move_branch_to_new_window->setVisible(has_branch);
        actions.move_branch_to_new_window->setEnabled(has_branch && static_cast<int>(m_controller->descendant_count(tab) + 1) < m_window.tab_count());
    }
    if (actions.close_branch)
        actions.close_branch->setVisible(has_branch);
}

void TreeTabsModule::attach_window_tab_layout_menu(QMenu& tab_layout_menu)
{
    m_window_layout_separator = tab_layout_menu.addSeparator();
    m_window_tree_tabs_action = tab_layout_menu.addAction("Organize Tabs as a &Tree");
    m_window_tree_tabs_action->setCheckable(true);
    QObject::connect(m_window_tree_tabs_action, &QAction::triggered, m_controller.ptr(), [](bool checked) {
        auto tab_settings = Ladybird::Application::settings().tab_settings();
        tab_settings.tree_tabs_enabled = checked;
        if (checked && !tab_settings.vertical_tabs_enabled)
            tab_settings.vertical_tabs_enabled = true;
        Ladybird::Application::settings().set_tab_settings(tab_settings);
    });
    update_window_tab_layout_menu();
}

void TreeTabsModule::update_window_tab_layout_menu()
{
    auto module_enabled = ModuleRegistry::the().is_enabled(MODULE_ID);
    if (m_window_layout_separator)
        m_window_layout_separator->setVisible(module_enabled);
    if (m_window_tree_tabs_action) {
        m_window_tree_tabs_action->setVisible(module_enabled);
        m_window_tree_tabs_action->setChecked(m_controller->is_enabled());
    }
}

void TreeTabsModule::append_tab_context(Ladybird::Tab& tab, Vector<String>& labels)
{
    if (!m_controller->is_enabled())
        return;
    auto const& model = m_controller->model();
    auto node_id = model.node_id_for_tab(&tab);
    if (!node_id.has_value())
        return;
    auto const* node = model.node(*node_id);
    if (!node || !node->parent_id.has_value())
        return;
    auto const* parent = model.node(*node->parent_id);
    if (!parent || !parent->tab)
        return;
    auto parent_title = parent->tab->title();
    if (parent_title.isEmpty())
        return;
    labels.append(ak_string_from_qstring(QString("in %1").arg(parent_title)));
}

namespace {

// Routes Tree-Style Tabs actions from the action bus to the active window's tree.
class ActiveWindowTreeTabsHandler final : public TreeTabsCommandHandler {
public:
    virtual bool can_run(TreeTabsCommand command) const override
    {
        if (command == TreeTabsCommand::ToggleMode)
            return true;

        auto* window = Ladybird::Application::the().active_window_if_any();
        if (!window)
            return false;
        auto* controller = window->superbird().tree_tab_controller();
        auto* tab = window->current_tab();
        if (!controller || !controller->is_enabled() || !tab)
            return false;

        switch (command) {
        case TreeTabsCommand::ToggleMode:
            return true;
        case TreeTabsCommand::NewChildTab:
        case TreeTabsCommand::IndentTab:
        case TreeTabsCommand::OutdentTab:
            return !tab->is_pinned();
        case TreeTabsCommand::ToggleBranch:
        case TreeTabsCommand::BookmarkBranch:
        case TreeTabsCommand::CloseBranch:
            return controller->has_children(*tab);
        case TreeTabsCommand::UnloadBranch:
            return controller->has_unloadable_tabs_in_branch(*tab);
        case TreeTabsCommand::MoveBranchToNewWindow:
            return controller->has_children(*tab) && static_cast<int>(controller->descendant_count(*tab) + 1) < window->tab_count();
        }
        VERIFY_NOT_REACHED();
    }

    virtual void run(TreeTabsCommand command) override
    {
        if (command == TreeTabsCommand::ToggleMode) {
            toggle_tree_tabs_mode();
            return;
        }
        if (!can_run(command))
            return;

        auto& window = Ladybird::Application::the().active_window();
        auto& controller = *window.superbird().tree_tab_controller();
        auto& tab = *window.current_tab();

        switch (command) {
        case TreeTabsCommand::ToggleMode:
            break;
        case TreeTabsCommand::NewChildTab:
            controller.new_child_tab(tab);
            break;
        case TreeTabsCommand::ToggleBranch:
            controller.toggle_branch_collapsed(tab);
            break;
        case TreeTabsCommand::UnloadBranch:
            controller.unload_branch(tab);
            break;
        case TreeTabsCommand::BookmarkBranch:
            controller.bookmark_branch(tab);
            break;
        case TreeTabsCommand::MoveBranchToNewWindow:
            controller.move_branch_to_new_window(tab);
            break;
        case TreeTabsCommand::CloseBranch:
            controller.close_branch(tab);
            break;
        case TreeTabsCommand::IndentTab:
            controller.indent_tab(tab);
            break;
        case TreeTabsCommand::OutdentTab:
            controller.outdent_tab(tab);
            break;
        }
    }

private:
    static void toggle_tree_tabs_mode()
    {
        auto tab_settings = Ladybird::Application::settings().tab_settings();
        tab_settings.tree_tabs_enabled = !(tab_settings.vertical_tabs_enabled && tab_settings.tree_tabs_enabled);
        if (tab_settings.tree_tabs_enabled)
            tab_settings.vertical_tabs_enabled = true;
        Ladybird::Application::settings().set_tab_settings(tab_settings);
    }
};

}

void TreeTabsModule::register_actions(ModuleRegistry& registry)
{
    static ActiveWindowTreeTabsHandler handler;
    register_tree_tabs_actions(registry, handler);
}

}
