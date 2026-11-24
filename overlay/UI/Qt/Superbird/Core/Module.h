/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Noncopyable.h>
#include <AK/StringView.h>
#include <AK/Vector.h>
#include <UI/Qt/Superbird/Core/Action.h>

class QMenu;

namespace Ladybird {
class BrowserWindow;
class Tab;
}

namespace Superbird {

// Static metadata describing a compiled-in Superbird module.
struct ModuleInfo {
    StringView id;
    StringView name;
    StringView description;
    bool enabled_by_default { true };
};

// Base interface implemented by each per-BrowserWindow Superbird module.
// Every hook has a no-op default so modules only override the lifecycle seams they actually need.
class WindowModule {
    AK_MAKE_NONCOPYABLE(WindowModule);
    AK_MAKE_NONMOVABLE(WindowModule);

public:
    WindowModule() = default;
    virtual ~WindowModule() = default;

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

    virtual void collect_actions(Vector<ActionDescriptor>&) const { }
};

}
