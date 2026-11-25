/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Array.h>
#include <AK/StringView.h>
#include <AK/Types.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/PaletteRanking.h>

#include <string_view>
#include <vector>

namespace Superbird {

class ModuleRegistry;

inline constexpr StringView COMMAND_PALETTE_MODULE_ID = "command_palette"sv;

// Everyday browser commands the Command Palette offers next to the modules' own actions. They go on the
// ModuleRegistry bus under the palette's module id, so they disappear along with the palette when it is disabled.
// Kept free of Qt so the table and its registration are unit-testable on their own.
enum class BrowserCommand : u8 {
    NewTab,
    NewWindow,
    NewPrivateWindow,
    CloseTab,
    DuplicateTab,
    TogglePinTab,
    ToggleMuteTab,
    Reload,
    FindInPage,
    ZoomIn,
    ZoomOut,
    ResetZoom,
    ToggleForceDark,
    BookmarkPage,
    ViewSource,
    OpenDownloads,
    OpenSettings,
};

struct BrowserActionDefinition {
    BrowserCommand command;
    StringView id;
    StringView title;
};

inline constexpr StringView BROWSER_ACTION_CATEGORY = "Browser"sv;

inline constexpr Array<BrowserActionDefinition, 17> BROWSER_ACTIONS { {
    { BrowserCommand::NewTab, "browser.new_tab"sv, "New Tab"sv },
    { BrowserCommand::NewWindow, "browser.new_window"sv, "New Window"sv },
    { BrowserCommand::NewPrivateWindow, "browser.new_private_window"sv, "New Private Window"sv },
    { BrowserCommand::CloseTab, "browser.close_tab"sv, "Close Current Tab"sv },
    { BrowserCommand::DuplicateTab, "browser.duplicate_tab"sv, "Duplicate Current Tab"sv },
    { BrowserCommand::TogglePinTab, "browser.toggle_pin_tab"sv, "Pin / Unpin Current Tab"sv },
    { BrowserCommand::ToggleMuteTab, "browser.toggle_mute_tab"sv, "Mute / Unmute Current Tab"sv },
    { BrowserCommand::Reload, "browser.reload"sv, "Reload Page"sv },
    { BrowserCommand::FindInPage, "browser.find_in_page"sv, "Find in Page"sv },
    { BrowserCommand::ZoomIn, "browser.zoom_in"sv, "Zoom In"sv },
    { BrowserCommand::ZoomOut, "browser.zoom_out"sv, "Zoom Out"sv },
    { BrowserCommand::ResetZoom, "browser.reset_zoom"sv, "Reset Zoom"sv },
    { BrowserCommand::ToggleForceDark, "browser.toggle_force_dark"sv, "Toggle Force Dark"sv },
    { BrowserCommand::BookmarkPage, "browser.bookmark_page"sv, "Bookmark Current Page"sv },
    { BrowserCommand::ViewSource, "browser.view_source"sv, "View Page Source"sv },
    { BrowserCommand::OpenDownloads, "browser.open_downloads"sv, "Open Downloads"sv },
    { BrowserCommand::OpenSettings, "browser.open_settings"sv, "Open Settings"sv },
} };

// Executes browser commands against whatever window is currently active.
class BrowserCommandHandler {
public:
    virtual ~BrowserCommandHandler() = default;
    virtual bool can_run(BrowserCommand) const = 0;
    virtual void run(BrowserCommand) = 0;
};

// Registers every browser action on the registry's action bus, routed to `handler`, which must outlive the registry's
// use of those actions.
void register_browser_actions(ModuleRegistry&, BrowserCommandHandler& handler);

// The palette rows for every action currently on offer: actions of enabled modules (or of no module) whose
// availability check passes. The entry key is the action id.
std::vector<Palette::Entry> collect_action_entries(ModuleRegistry const&);

// Triggers the action with this id, if it is still on offer. Returns whether it ran.
bool trigger_action(ModuleRegistry const&, std::string_view action_id);

}
