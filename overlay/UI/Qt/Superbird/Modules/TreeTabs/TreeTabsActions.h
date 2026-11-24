/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Array.h>
#include <AK/StringView.h>
#include <AK/Types.h>

namespace Superbird {

class ModuleRegistry;

inline constexpr StringView TREE_TABS_MODULE_ID = "tree_tabs"sv;

// The Tree-Style Tabs commands exposed on the Superbird action bus (Command Palette, menus, shortcuts).
// Kept free of Qt so the table and its registration are unit-testable on their own.
enum class TreeTabsCommand : u8 {
    ToggleMode,
    NewChildTab,
    ToggleBranch,
    UnloadBranch,
    BookmarkBranch,
    MoveBranchToNewWindow,
    CloseBranch,
    IndentTab,
    OutdentTab,
};

struct TreeTabsActionDefinition {
    TreeTabsCommand command;
    StringView id;
    StringView title;
};

inline constexpr StringView TREE_TABS_ACTION_CATEGORY = "Tree Tabs"sv;

inline constexpr Array<TreeTabsActionDefinition, 9> TREE_TABS_ACTIONS { {
    { TreeTabsCommand::ToggleMode, "tree_tabs.toggle_mode"sv, "Toggle Tree-Style Tabs"sv },
    { TreeTabsCommand::NewChildTab, "tree_tabs.new_child_tab"sv, "New Child Tab"sv },
    { TreeTabsCommand::ToggleBranch, "tree_tabs.toggle_branch"sv, "Collapse / Expand Current Branch"sv },
    { TreeTabsCommand::UnloadBranch, "tree_tabs.unload_branch"sv, "Unload Current Branch"sv },
    { TreeTabsCommand::BookmarkBranch, "tree_tabs.bookmark_branch"sv, "Bookmark Current Branch"sv },
    { TreeTabsCommand::MoveBranchToNewWindow, "tree_tabs.move_branch_to_new_window"sv, "Move Current Branch to New Window"sv },
    { TreeTabsCommand::CloseBranch, "tree_tabs.close_branch"sv, "Close Current Branch"sv },
    { TreeTabsCommand::IndentTab, "tree_tabs.indent_tab"sv, "Indent Current Tab"sv },
    { TreeTabsCommand::OutdentTab, "tree_tabs.outdent_tab"sv, "Outdent Current Tab"sv },
} };

// Executes Tree-Style Tabs commands against whatever window is currently active.
class TreeTabsCommandHandler {
public:
    virtual ~TreeTabsCommandHandler() = default;
    virtual bool can_run(TreeTabsCommand) const = 0;
    virtual void run(TreeTabsCommand) = 0;
};

// Registers every Tree-Style Tabs action on the registry's action bus, routed to `handler`, which must outlive the
// registry's use of those actions.
void register_tree_tabs_actions(ModuleRegistry&, TreeTabsCommandHandler& handler);

}
