/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Forward.h>
#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/Noncopyable.h>
#include <AK/StringView.h>
#include <AK/Vector.h>

#include <string>

class QMenu;

namespace Ladybird {
class BrowserWindow;
class Menu;
class Tab;
}

namespace URL {
class URL;
}

namespace Superbird {

struct ModuleChoice {
    StringView value;
    StringView label;
};

// An option a module offers under its switch in about:settings -> Superbird. Its value is text: "true" or "false" for a
// switch, one of the choices' values for a choice.
struct ModuleOption {
    enum class Kind : u8 {
        Switch,
        Choice,
    };
    StringView id;
    StringView label;
    StringView description {};
    Kind kind { Kind::Switch };
    StringView default_value { "true"sv };
    ReadonlySpan<ModuleChoice> choices {};
};

// A command with a keyboard shortcut the user can change in about:settings. The command is the action on the
// ModuleRegistry bus with this id, which WindowHost triggers when the shortcut is pressed. Shortcuts are Qt's portable
// key sequence text ("Ctrl+K", where Ctrl is Command on macOS), several separated by "; ", or empty for none.
struct ModuleShortcut {
    StringView command_id;
    StringView title;
    StringView default_shortcut {};
};

// Static metadata describing a compiled-in Superbird module.
struct ModuleInfo {
    StringView id;
    StringView name;
    StringView description;
    bool enabled_by_default { true };
    ReadonlySpan<ModuleOption> options {};
    ReadonlySpan<ModuleShortcut> shortcuts {};
};

// The options and shortcuts a module offers, as the JSON about:settings draws them from.
std::string module_settings_schema_json(ModuleInfo const&);

// Interface implemented by every Superbird module. WindowHost creates one instance per BrowserWindow and fans
// window/tab lifecycle, menu, settings, and style hooks out to it. Every hook has a no-op default so modules only
// override the seams they actually need. App-wide actions go on the ModuleRegistry action bus instead.
class SuperbirdModule {
    AK_MAKE_NONCOPYABLE(SuperbirdModule);
    AK_MAKE_NONMOVABLE(SuperbirdModule);

public:
    SuperbirdModule() = default;
    virtual ~SuperbirdModule() = default;

    virtual StringView id() const = 0;

    virtual void on_enabled_changed(bool) { }
    virtual void on_settings_changed() { }
    virtual void on_chrome_style_changed() { }

    virtual void on_pending_opener(Ladybird::Tab*) { }
    virtual void on_tab_added(Ladybird::Tab&) { }
    virtual void on_tab_removed(Ladybird::Tab&) { }
    virtual void on_tab_replaced(Ladybird::Tab const&, Ladybird::Tab&) { }
    virtual void on_tab_session_id_assigned(Ladybird::Tab&) { }
    virtual void on_tab_pinned_changed(Ladybird::Tab&) { }
    virtual void on_active_tab_changed(Ladybird::Tab*) { }
    virtual void on_tab_metadata_changed() { }
    virtual void on_tab_moved_externally() { }
    virtual void on_tabs_restored() { }

    virtual void attach_tab_context_menu(Ladybird::Tab&, QMenu&) { }
    virtual void update_tab_context_menu(Ladybird::Tab&) { }

    virtual void attach_window_tab_layout_menu(QMenu&) { }
    virtual void update_window_tab_layout_menu() { }

    // Appends short labels that place a tab in this module's structure (its tree branch, its workspace, ...), for
    // surfaces such as the Command Palette that list tabs outside their usual context.
    virtual void append_tab_context(Ladybird::Tab&, Vector<String>&) { }

    // Whether this module keeps a tab out of the tab bar and sidebar (e.g. it's in another workspace). A tab shows
    // only if no enabled module hides it. WindowHost::update_tab_visibility() applies the answers.
    virtual bool is_tab_hidden(Ladybird::Tab&) const { return false; }
    virtual void on_tab_visibility_changed() { }

    // A color (0xRRGGBB) this module marks the tab with in the tab bar and sidebar (e.g. Split View's accent for the tabs
    // of its split). The first enabled module with one wins. WindowHost::tab_markers_changed() asks for a redraw.
    virtual Optional<u32> tab_marker_rgb(Ladybird::Tab&) const { return {}; }
    virtual void on_tab_markers_changed() { }

    // Adds items to the context menu of a link in one of this window's tabs (e.g. "Peek in Glance").
    virtual void extend_link_context_menu(Ladybird::Tab&, Ladybird::Menu&, URL::URL const&) { }
    // Adds items to the context menu of an editable text field in one of this window's tabs (e.g. Passwords autofill).
    virtual void extend_editable_context_menu(Ladybird::Tab&, Ladybird::Menu&, URL::URL const&) { }
};

}
