/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Application.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/HubConnection.h>
#include <UI/Qt/Tab.h>
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
#if defined(SUPERBIRD_MODULE_SPLIT_VIEW)
#    include <UI/Qt/Superbird/Modules/SplitView/SplitViewModule.h>
#endif
#if defined(SUPERBIRD_MODULE_GLANCE)
#    include <UI/Qt/Superbird/Modules/Glance/GlanceModule.h>
#endif
#if defined(SUPERBIRD_MODULE_CONTENT_BLOCKING)
#    include <UI/Qt/Superbird/Modules/ContentBlocking/ContentBlockingModule.h>
#endif
#if defined(SUPERBIRD_MODULE_PASSWORDS)
#    include <UI/Qt/Superbird/Modules/Passwords/PasswordsModule.h>
#endif

#include <QApplication>
#include <QHash>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QShortcut>
#include <QTimer>

namespace Superbird {

// Registers a compiled module once: with the module registry, on the action bus, and in the settings catalog that
// about:settings -> Superbird lists toggles from.
[[maybe_unused]] static void register_compiled_module(ModuleInfo const& info, void (*register_actions)(ModuleRegistry&))
{
    auto& registry = ModuleRegistry::the();
    if (!registry.register_module(info))
        return;

    register_actions(registry);

    auto id = MUST(String::from_utf8(info.id));
    auto name = MUST(String::from_utf8(info.name));
    auto description = MUST(String::from_utf8(info.description));
    auto schema_json = module_settings_schema_json(info);
    auto schema = MUST(String::from_utf8(StringView { schema_json.data(), schema_json.size() }));
    Ladybird::Application::settings().register_superbird_module({
        .id = id,
        .name = name,
        .description = description,
        .enabled_by_default = info.enabled_by_default,
        .settings_schema = schema,
    });
    // NB: about:settings is served by the Hub, which keeps its own settings, so it hears of the module too.
    if (auto* hub = Ladybird::HubConnection::the_if_connected())
        hub->async_register_superbird_module(move(id), move(name), move(description), info.enabled_by_default, move(schema));
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
#if defined(SUPERBIRD_MODULE_SPLIT_VIEW)
    register_compiled_module(SplitViewModule::module_info(), SplitViewModule::register_actions);
#endif
#if defined(SUPERBIRD_MODULE_GLANCE)
    register_compiled_module(GlanceModule::module_info(), GlanceModule::register_actions);
#endif
#if defined(SUPERBIRD_MODULE_CONTENT_BLOCKING)
    register_compiled_module(ContentBlockingModule::module_info(), ContentBlockingModule::register_actions);
#endif
#if defined(SUPERBIRD_MODULE_PASSWORDS)
    register_compiled_module(PasswordsModule::module_info(), PasswordsModule::register_actions);
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

static String to_string(StringView text)
{
    return MUST(String::from_utf8(text));
}

String WindowHost::module_option(StringView module_id, StringView option_id)
{
    auto const& settings = Ladybird::Application::settings().superbird_settings();
    if (auto options = settings.module_options.get(to_string(module_id)); options.has_value()) {
        if (auto value = options->get(to_string(option_id)); value.has_value())
            return *value;
    }
    if (auto info = ModuleRegistry::the().module_info(module_id); info.has_value()) {
        for (auto const& option : info->options) {
            if (option.id == option_id)
                return to_string(option.default_value);
        }
    }
    return {};
}

bool WindowHost::module_option_is_on(StringView module_id, StringView option_id)
{
    return module_option(module_id, option_id) == "true"sv;
}

static QList<QKeySequence> parse_shortcuts(StringView text)
{
    QList<QKeySequence> sequences;
    // NB: The list separator is "; ", so a shortcut of Ctrl+; still reads as one.
    auto text_string = QString::fromUtf8(text.characters_without_null_termination(), static_cast<qsizetype>(text.length()));
    for (auto const& sequence : QKeySequence::listFromString(text_string, QKeySequence::PortableText)) {
        if (sequence.isEmpty() || sequence[0].key() == Qt::Key_unknown)
            continue;
        if (!sequences.contains(sequence))
            sequences.append(sequence);
    }
    return sequences;
}

QList<QKeySequence> WindowHost::shortcuts_for(StringView command_id)
{
    auto const& settings = Ladybird::Application::settings().superbird_settings();
    if (auto picked = settings.shortcuts.get(to_string(command_id)); picked.has_value())
        return parse_shortcuts(*picked);
    for (auto const& info : ModuleRegistry::the().modules()) {
        for (auto const& shortcut : info.shortcuts) {
            if (shortcut.command_id == command_id)
                return parse_shortcuts(shortcut.default_shortcut);
        }
    }
    return {};
}

QString WindowHost::shortcut_text(StringView command_id)
{
    auto sequences = shortcuts_for(command_id);
    if (sequences.isEmpty())
        return {};
    return sequences.first().toString(QKeySequence::NativeText);
}

namespace {

// Text fields claim some keys for editing (Ctrl+K deletes to the end of the line on Linux); in this window's fields
// the window's Superbird shortcuts win instead.
class FieldShortcutFilter final : public QObject {
public:
    FieldShortcutFilter(QWidget& window, Function<bool(QKeyCombination)> is_shortcut)
        : QObject(&window)
        , m_window(&window)
        , m_is_shortcut(AK::move(is_shortcut))
    {
    }

private:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::ShortcutOverride)
            return false;
        auto* field = qobject_cast<QLineEdit*>(watched);
        if (!field || field->window() != m_window)
            return false;
        auto const& key_event = *static_cast<QKeyEvent*>(event);
        auto modifiers = key_event.modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        if (!m_is_shortcut(QKeyCombination(modifiers, static_cast<Qt::Key>(key_event.key()))))
            return false;
        // Not taken by the field, so the window's shortcut fires.
        event->ignore();
        return true;
    }

    QPointer<QWidget> m_window;
    Function<bool(QKeyCombination)> m_is_shortcut;
};

}

void WindowHost::install_shortcuts()
{
    remove_shortcuts();

    auto& registry = ModuleRegistry::the();
    bool takes_plain_digits = false;
    for (auto const& info : registry.modules()) {
        if (!registry.is_enabled(info.id))
            continue;
        for (auto const& command : info.shortcuts) {
            for (auto const& sequence : shortcuts_for(command.command_id)) {
                // The first command to claim a key keeps it; a second shortcut for it would make both ambiguous.
                if (m_reserved_shortcuts.contains_slow(sequence))
                    continue;
                auto* shortcut = new QShortcut(sequence, &m_window);
                shortcut->setContext(Qt::WindowShortcut);
                QObject::connect(shortcut, &QShortcut::activated, &m_window, [command_id = command.command_id] {
                    auto const* action = ModuleRegistry::the().action(command_id);
                    if (action && action->trigger && action->available())
                        action->trigger();
                });
                m_shortcuts.append(shortcut);
                m_reserved_shortcuts.append(sequence);
                reserve_shortcut(sequence);

                auto combination = sequence[0];
                if (sequence.count() == 1 && combination.keyboardModifiers() == Qt::ControlModifier
                    && combination.key() >= Qt::Key_1 && combination.key() <= Qt::Key_9)
                    takes_plain_digits = true;
            }
        }
    }
    // Cmd/Ctrl+1-9 pick tabs by number only while no Superbird command has one of them (Workspaces does by default).
    m_window.set_plain_tab_number_shortcuts_enabled(!takes_plain_digits);

    if (!m_shortcuts.is_empty() && !m_field_shortcut_filter) {
        m_field_shortcut_filter = new FieldShortcutFilter(m_window, [this](QKeyCombination combination) {
            return m_reserved_shortcuts.contains_slow(QKeySequence(combination));
        });
        QApplication::instance()->installEventFilter(m_field_shortcut_filter);
    }
}

void WindowHost::remove_shortcuts()
{
    if (m_field_shortcut_filter) {
        QApplication::instance()->removeEventFilter(m_field_shortcut_filter);
        delete m_field_shortcut_filter.data();
    }
    for (auto const& sequence : m_reserved_shortcuts)
        release_shortcut(sequence);
    m_reserved_shortcuts.clear();
    for (auto& shortcut : m_shortcuts)
        delete shortcut.data();
    m_shortcuts.clear();
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
#if defined(SUPERBIRD_MODULE_SPLIT_VIEW)
    m_modules.append(make<SplitViewModule>(m_window));
#endif
#if defined(SUPERBIRD_MODULE_GLANCE)
    m_modules.append(make<GlanceModule>(m_window));
#endif
#if defined(SUPERBIRD_MODULE_CONTENT_BLOCKING)
    m_modules.append(make<ContentBlockingModule>(m_window));
#endif
#if defined(SUPERBIRD_MODULE_PASSWORDS)
    m_modules.append(make<PasswordsModule>(m_window));
#endif

    ModuleRegistry::the().add_observer(*this);

    // NB: The window makes its own tab-number shortcuts after its WindowHost, so the shortcuts that may replace them
    //     wait for it.
    QTimer::singleShot(0, &m_window, [this] { install_shortcuts(); });
}

WindowHost::~WindowHost()
{
    remove_shortcuts();
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
    install_shortcuts();
}

void WindowHost::on_settings_changed()
{
    sync_registry_from_settings();
    for (auto& module : m_modules)
        module->on_settings_changed();
    install_shortcuts();
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

void WindowHost::extend_link_context_menu(Ladybird::RemoteView& view, Ladybird::Menu& menu, URL::URL const& url)
{
    for (auto* widget : QApplication::topLevelWidgets()) {
        auto* window = dynamic_cast<Ladybird::BrowserWindow*>(widget);
        if (!window)
            continue;
        auto* tab = window->tab_for_view(view);
        if (!tab)
            continue;
        auto& host = window->superbird();
        for (auto& module : host.m_modules) {
            if (ModuleRegistry::the().is_enabled(module->id()))
                module->extend_link_context_menu(*tab, menu, url);
        }
        return;
    }
}

void WindowHost::extend_editable_context_menu(Ladybird::RemoteView& view, Ladybird::Menu& menu, URL::URL const& url)
{
    for (auto* widget : QApplication::topLevelWidgets()) {
        auto* window = dynamic_cast<Ladybird::BrowserWindow*>(widget);
        if (!window)
            continue;
        auto* tab = window->tab_for_view(view);
        if (!tab)
            continue;
        auto& host = window->superbird();
        for (auto& module : host.m_modules) {
            if (ModuleRegistry::the().is_enabled(module->id()))
                module->extend_editable_context_menu(*tab, menu, url);
        }
        return;
    }
}

bool WindowHost::is_tab_hidden(Ladybird::Tab& tab) const
{
    for (auto const& module : m_modules) {
        if (ModuleRegistry::the().is_enabled(module->id()) && module->is_tab_hidden(tab))
            return true;
    }
    return false;
}

Optional<QColor> WindowHost::tab_marker_color(Ladybird::Tab& tab)
{
    auto* window = tab.browser_window();
    if (!window)
        return {};
    for (auto const& module : window->superbird().m_modules) {
        if (!ModuleRegistry::the().is_enabled(module->id()))
            continue;
        if (auto rgb = module->tab_marker_rgb(tab); rgb.has_value())
            return QColor::fromRgb(*rgb);
    }
    return {};
}

void WindowHost::tab_markers_changed()
{
    if (auto* tab_bar = m_window.tab_widget().tab_bar())
        tab_bar->update();
    for (auto& module : m_modules)
        module->on_tab_markers_changed();
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
