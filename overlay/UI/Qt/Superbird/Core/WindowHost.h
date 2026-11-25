/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#include <AK/StringView.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>

class QKeyEvent;
class QKeySequence;
class QMenu;

namespace Ladybird {
class BrowserWindow;
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
    static void reserve_shortcut(QKeySequence const&);
    static void release_shortcut(QKeySequence const&);
    static bool is_reserved_shortcut(QKeyEvent const&);

    explicit WindowHost(Ladybird::BrowserWindow& window);
    virtual ~WindowHost() override;

    Ladybird::TreeTabController* tree_tab_controller() const;

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

private:
    virtual void module_enabled_changed(StringView module_id, bool enabled) override;

    [[maybe_unused]] Ladybird::BrowserWindow& m_window;
    Vector<NonnullOwnPtr<SuperbirdModule>> m_modules;
    [[maybe_unused]] TreeTabsModule* m_tree_tabs_module { nullptr };
};

}
