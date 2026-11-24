/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Application.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>

#if defined(SUPERBIRD_MODULE_TREE_TABS)
#    include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabController.h>
#    include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabsModule.h>
#endif

namespace Superbird {

void WindowHost::register_compiled_modules()
{
    auto& registry = ModuleRegistry::the();
#if defined(SUPERBIRD_MODULE_TREE_TABS)
    registry.register_module(TreeTabsModule::module_info());
#endif
}

void WindowHost::sync_registry_from_settings()
{
    register_compiled_modules();
    auto const& superbird_settings = Ladybird::Application::settings().superbird_settings();
    ModuleRegistry::the().sync_from_overrides(superbird_settings.enabled_modules);
}

WindowHost::WindowHost(Ladybird::BrowserWindow& window)
    : m_window(window)
{
    sync_registry_from_settings();

#if defined(SUPERBIRD_MODULE_TREE_TABS)
    auto tree_tabs = make<TreeTabsModule>(m_window);
    m_tree_tabs_module = tree_tabs.ptr();
    m_modules.append(move(tree_tabs));
#endif

    ModuleRegistry::the().add_observer(*this);
}

WindowHost::~WindowHost()
{
    ModuleRegistry::the().remove_observer(*this);
}

Ladybird::TreeTabController* WindowHost::tree_tab_controller() const
{
#if defined(SUPERBIRD_MODULE_TREE_TABS)
    return m_tree_tabs_module ? &m_tree_tabs_module->controller() : nullptr;
#else
    return nullptr;
#endif
}

void WindowHost::module_enabled_changed(StringView module_id, bool enabled)
{
    for (auto& module : m_modules) {
        if (module->id() == module_id)
            module->on_enabled_changed(enabled);
    }
}

void WindowHost::on_settings_changed()
{
    sync_registry_from_settings();
    for (auto& module : m_modules)
        module->on_settings_changed();
}

void WindowHost::on_chrome_style_changed()
{
    for (auto& module : m_modules)
        module->on_chrome_style_changed();
}

void WindowHost::on_pending_opener(Ladybird::Tab* opener)
{
    for (auto& module : m_modules)
        module->on_pending_opener(opener);
}

void WindowHost::on_tab_added(Ladybird::Tab& tab)
{
    for (auto& module : m_modules)
        module->on_tab_added(tab);
}

void WindowHost::on_tab_removed(Ladybird::Tab& tab)
{
    for (auto& module : m_modules)
        module->on_tab_removed(tab);
}

void WindowHost::on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab)
{
    for (auto& module : m_modules)
        module->on_tab_replaced(old_tab, new_tab);
}

void WindowHost::on_tab_session_id_assigned(Ladybird::Tab& tab)
{
    for (auto& module : m_modules)
        module->on_tab_session_id_assigned(tab);
}

void WindowHost::on_tab_pinned_changed(Ladybird::Tab& tab)
{
    for (auto& module : m_modules)
        module->on_tab_pinned_changed(tab);
}

void WindowHost::on_active_tab_changed(Ladybird::Tab* tab)
{
    for (auto& module : m_modules)
        module->on_active_tab_changed(tab);
}

void WindowHost::on_tab_metadata_changed()
{
    for (auto& module : m_modules)
        module->on_tab_metadata_changed();
}

void WindowHost::on_tab_moved_externally()
{
    for (auto& module : m_modules)
        module->on_tab_moved_externally();
}

void WindowHost::on_tabs_restored()
{
    for (auto& module : m_modules)
        module->on_tabs_restored();
}

void WindowHost::attach_tab_context_menu(Ladybird::Tab& tab, QMenu& menu)
{
    for (auto& module : m_modules)
        module->attach_tab_context_menu(tab, menu);
}

void WindowHost::update_tab_context_menu(Ladybird::Tab& tab)
{
    for (auto& module : m_modules)
        module->update_tab_context_menu(tab);
}

void WindowHost::attach_window_tab_layout_menu(QMenu& tab_layout_menu)
{
    for (auto& module : m_modules)
        module->attach_window_tab_layout_menu(tab_layout_menu);
}

void WindowHost::update_window_tab_layout_menu()
{
    for (auto& module : m_modules)
        module->update_window_tab_layout_menu();
}

Vector<ActionDescriptor> WindowHost::collect_actions(Optional<StringView> query) const
{
    Vector<ActionDescriptor> all;
    for (auto const& module : m_modules) {
        if (!ModuleRegistry::the().is_enabled(module->id()))
            continue;
        module->collect_actions(all);
    }

    auto filter = query.value_or(""sv);
    Vector<ActionDescriptor> filtered;
    for (auto& action : all) {
        if (!action.available())
            continue;
        if (!ModuleRegistry::matches_query(action, filter))
            continue;
        filtered.append(move(action));
    }
    return filtered;
}

}
