/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/StringView.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>

#include <QColor>
#include <QKeySequence>
#include <QList>
#include <QPointer>
#include <QString>

class QKeyEvent;
class QShortcut;
class QMenu;

namespace Ladybird {
class BrowserWindow;
class Menu;
class RemoteView;
class Tab;
class TreeTabController;
}

namespace Superbird {

class TreeTabsModule;

// Single per-BrowserWindow seam that owns all compiled SuperbirdModules and fans out
// lifecycle, menu, and settings hooks so BrowserWindow stays minimal on upstream rebases.
class WindowHost final : public ModuleRegistryObserver {
    AK_MAKE_NONCOPYABLE(WindowHost);
    AK_MAKE_NONMOVABLE(WindowHost);

public:
    AK_ALLOC_WITH_KMALLOC;

    static void register_compiled_modules();
    static void sync_registry_from_settings();

    // App-wide keyboard shortcuts that modules claim while they are enabled. A page with focus otherwise gets the
    // first say over every key (WebContentView accepts ShortcutOverride), so a module's window shortcut would never
    // fire. Reservations are counted, so each window's module instance reserves and releases its own.
    // Where the profile keeps its data, for modules' sidecar databases, or nothing if this run keeps no data (its
    // sessions aren't persisted). Set once by the application before any window exists.
    static void set_profile_data_directory(ByteString data_directory, bool persists);
    static Optional<ByteString> const& persistent_data_directory();

    static void reserve_shortcut(QKeySequence const&);
    static void release_shortcut(QKeySequence const&);
    static bool is_reserved_shortcut(QKeyEvent const&);

    // A module's option as about:settings sets it, or its default.
    static String module_option(StringView module_id, StringView option_id);
    static bool module_option_is_on(StringView module_id, StringView option_id);
    // The shortcuts of a command (see ModuleShortcut): the ones picked in about:settings, or its defaults.
    static QList<QKeySequence> shortcuts_for(StringView command_id);
    // The first of them, as the platform writes it ("⌘K"), or nothing.
    static QString shortcut_text(StringView command_id);

    // Lets the modules of the window showing this view add to a link's context menu.
    static void extend_link_context_menu(Ladybird::RemoteView&, Ladybird::Menu&, URL::URL const&);

    explicit WindowHost(Ladybird::BrowserWindow& window);
    virtual ~WindowHost() override;

    Ladybird::TreeTabController* tree_tab_controller() const;
    // This window's instance of a compiled module, whether or not it is enabled.
    SuperbirdModule* module(StringView id) const;

    void on_settings_changed();
    void on_chrome_style_changed();

    void on_pending_opener(Ladybird::Tab* opener);
    void on_tab_added(Ladybird::Tab& tab);
    void on_tab_removed(Ladybird::Tab& tab);
    void on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab);
    void on_tab_session_id_assigned(Ladybird::Tab& tab);
    void on_tab_pinned_changed(Ladybird::Tab& tab);
    void on_active_tab_changed(Ladybird::Tab* tab);
    void on_tab_metadata_changed();
    void on_tab_moved_externally();
    void on_tabs_restored();

    void attach_tab_context_menu(Ladybird::Tab& tab, QMenu& menu);
    void update_tab_context_menu(Ladybird::Tab& tab);

    void attach_window_tab_layout_menu(QMenu& tab_layout_menu);
    void update_window_tab_layout_menu();

    // The labels every enabled module gives this tab's context, in module order.
    Vector<String> tab_context(Ladybird::Tab& tab);

    bool is_tab_hidden(Ladybird::Tab& tab) const;
    // The marker the tab's window's modules give it, if any (see SuperbirdModule::tab_marker_color()).
    static Optional<QColor> tab_marker_color(Ladybird::Tab& tab);
    // Redraws the tab bar and tells the modules (Tree Tabs redraws) after a module's markers changed.
    void tab_markers_changed();
    // Shows and hides tabs in the tab bar as the modules say, and tells the modules (Tree Tabs redraws). With
    // HideTabs::No it only shows tabs, so a module can bring the next tab into view before hiding the current one.
    enum class HideTabs : bool {
        No,
        Yes,
    };
    void update_tab_visibility(HideTabs = HideTabs::Yes);

private:
    virtual void module_enabled_changed(StringView module_id, bool enabled) override;

    // Sets up this window's shortcuts for the enabled modules' commands, replacing the ones it had.
    void install_shortcuts();
    void remove_shortcuts();

    [[maybe_unused]] Ladybird::BrowserWindow& m_window;
    Vector<NonnullOwnPtr<SuperbirdModule>> m_modules;
    [[maybe_unused]] TreeTabsModule* m_tree_tabs_module { nullptr };
    Vector<QPointer<QShortcut>> m_shortcuts;
    Vector<QKeySequence> m_reserved_shortcuts;
    QPointer<QObject> m_field_shortcut_filter;
};

}
