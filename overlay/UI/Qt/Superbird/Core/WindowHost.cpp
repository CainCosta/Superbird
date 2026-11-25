/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Application.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/TabBar.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>

#if defined(SUPERBIRD_MODULE_TREE_TABS)
#    include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabController.h>
#    include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabsModule.h>
#endif
#if defined(SUPERBIRD_MODULE_COMMAND_PALETTE)
#    include <UI/Qt/Superbird/Modules/CommandPalette/CommandPaletteModule.h>
#endif
#if defined(SUPERBIRD_MODULE_READER_VIEW)
#    include <UI/Qt/Superbird/Modules/ReaderView/ReaderViewModule.h>
#endif
#if defined(SUPERBIRD_MODULE_WORKSPACES)
#    include <UI/Qt/Superbird/Modules/Workspaces/WorkspacesModule.h>
#endif

#include <QHash>
#include <QKeyEvent>
#include <QKeySequence>

namespace Superbird {

// Registers a compiled module once: with the module registry, on the action bus, and in the settings catalog that
// about:settings -> Superbird lists toggles from.
[[maybe_unused]] static void register_compiled_module(ModuleInfo const& info, void (*register_actions)(ModuleRegistry&))
{
    auto& registry = ModuleRegistry::the();
    if (!registry.register_module(info))
        return;

    register_actions(registry);

    Ladybird::Application::settings().register_superbird_module({
        .id = MUST(String::from_utf8(info.id)),
        .name = MUST(String::from_utf8(info.name)),
        .description = MUST(String::from_utf8(info.description)),
        .enabled_by_default = info.enabled_by_default,
    });
}

void WindowHost::register_compiled_modules()
{
#if defined(SUPERBIRD_MODULE_TREE_TABS)
    register_compiled_module(TreeTabsModule::module_info(), TreeTabsModule::register_actions);
#endif
#if defined(SUPERBIRD_MODULE_COMMAND_PALETTE)
    register_compiled_module(CommandPaletteModule::module_info(), CommandPaletteModule::register_actions);
#endif
#if defined(SUPERBIRD_MODULE_READER_VIEW)
    register_compiled_module(ReaderViewModule::module_info(), ReaderViewModule::register_actions);
#endif
#if defined(SUPERBIRD_MODULE_WORKSPACES)
    register_compiled_module(WorkspacesModule::module_info(), WorkspacesModule::register_actions);
#endif
}

static Optional<ByteString>& profile_data_directory()
{
    static Optional<ByteString> directory;
    return directory;
}

void WindowHost::set_profile_data_directory(ByteString data_directory, bool persists)
{
    if (persists)
        profile_data_directory() = move(data_directory);
    else
        profile_data_directory().clear();
}

Optional<ByteString> const& WindowHost::persistent_data_directory()
{
    return profile_data_directory();
}

static QHash<QKeyCombination, int>& reserved_shortcuts()
{
    static QHash<QKeyCombination, int> shortcuts;
    return shortcuts;
}

void WindowHost::reserve_shortcut(QKeySequence const& sequence)
{
    // Only single-chord sequences can be matched against one key event.
    if (sequence.count() != 1)
        return;
    ++reserved_shortcuts()[sequence[0]];
}

void WindowHost::release_shortcut(QKeySequence const& sequence)
{
    if (sequence.count() != 1)
        return;
    auto& shortcuts = reserved_shortcuts();
    auto it = shortcuts.find(sequence[0]);
    if (it == shortcuts.end())
        return;
    if (--it.value() <= 0)
        shortcuts.erase(it);
}

bool WindowHost::is_reserved_shortcut(QKeyEvent const& event)
{
    auto const& shortcuts = reserved_shortcuts();
    if (shortcuts.isEmpty())
        return false;
    auto modifiers = event.modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    return shortcuts.contains(QKeyCombination(modifiers, static_cast<Qt::Key>(event.key())));
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
#if defined(SUPERBIRD_MODULE_COMMAND_PALETTE)
    m_modules.append(make<CommandPaletteModule>(m_window));
#endif
#if defined(SUPERBIRD_MODULE_READER_VIEW)
    m_modules.append(make<ReaderViewModule>(m_window));
#endif
#if defined(SUPERBIRD_MODULE_WORKSPACES)
    m_modules.append(make<WorkspacesModule>(m_window));
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

SuperbirdModule* WindowHost::module(StringView id) const
{
    for (auto const& module : m_modules) {
        if (module->id() == id)
            return module.ptr();
    }
    return nullptr;
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

bool WindowHost::is_tab_hidden(Ladybird::Tab& tab) const
{
    for (auto const& module : m_modules) {
        if (ModuleRegistry::the().is_enabled(module->id()) && module->is_tab_hidden(tab))
            return true;
    }
    return false;
}

void WindowHost::update_tab_visibility(HideTabs hide_tabs)
{
    auto& tab_widget = m_window.tab_widget();
    auto* tab_bar = tab_widget.tab_bar();
    bool changed = false;
    for (int index = 0; index < m_window.tab_count(); ++index) {
        auto* tab = m_window.tab_at(index);
        auto visible = !tab || !is_tab_hidden(*tab);
        if (!visible && hide_tabs == HideTabs::No)
            continue;
        if (tab_bar->isTabVisible(index) != visible) {
            tab_bar->setTabVisible(index, visible);
            changed = true;
        }
    }
    if (changed)
        tab_widget.update_tab_layout();
    for (auto& module : m_modules)
        module->on_tab_visibility_changed();
}

Vector<String> WindowHost::tab_context(Ladybird::Tab& tab)
{
    Vector<String> labels;
    for (auto& module : m_modules) {
        if (ModuleRegistry::the().is_enabled(module->id()))
            module->append_tab_context(tab, labels);
    }
    return labels;
}

}
