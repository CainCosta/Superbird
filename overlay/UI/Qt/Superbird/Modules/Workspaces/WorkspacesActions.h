/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Array.h>
#include <AK/StringView.h>
#include <AK/Types.h>

#include <span>
#include <string>

namespace Superbird {

class ModuleRegistry;

inline constexpr StringView WORKSPACES_MODULE_ID = "workspaces"sv;
inline constexpr StringView WORKSPACES_ACTION_CATEGORY = "Workspaces"sv;
// "Switch to Workspace: <name>" actions for the first workspaces of the active window.
inline constexpr size_t WORKSPACE_SWITCH_SLOTS = 9;

// The Workspaces commands on the Superbird action bus. Kept free of Qt so the table, its registration and the
// per-name switch actions are unit-testable on their own.
enum class WorkspacesCommand : u8 {
    NewWorkspace,
    RenameWorkspace,
    DeleteWorkspace,
    NextWorkspace,
    PreviousWorkspace,
    MoveTabToNextWorkspace,
    SwitchToWorkspace,
};

struct WorkspacesActionDefinition {
    WorkspacesCommand command;
    StringView id;
    StringView title;
};

inline constexpr Array<WorkspacesActionDefinition, 6> WORKSPACES_ACTIONS { {
    { WorkspacesCommand::NewWorkspace, "workspaces.new"sv, "New Workspace"sv },
    { WorkspacesCommand::RenameWorkspace, "workspaces.rename"sv, "Rename Current Workspace"sv },
    { WorkspacesCommand::DeleteWorkspace, "workspaces.delete"sv, "Delete Current Workspace"sv },
    { WorkspacesCommand::NextWorkspace, "workspaces.next"sv, "Next Workspace"sv },
    { WorkspacesCommand::PreviousWorkspace, "workspaces.previous"sv, "Previous Workspace"sv },
    { WorkspacesCommand::MoveTabToNextWorkspace, "workspaces.move_tab_to_next"sv, "Move Current Tab to Next Workspace"sv },
} };

class WorkspacesCommandHandler {
public:
    virtual ~WorkspacesCommandHandler() = default;
    // `slot` is the workspace position for SwitchToWorkspace, and unused otherwise.
    virtual bool can_run(WorkspacesCommand, size_t slot) const = 0;
    virtual void run(WorkspacesCommand, size_t slot) = 0;
};

// Registers the fixed actions plus WORKSPACE_SWITCH_SLOTS switch actions (unavailable until named).
void register_workspaces_actions(ModuleRegistry&, WorkspacesCommandHandler& handler);

// Retitles the switch actions after the given workspace names ("Switch to Workspace: Work"). Slots past the end of
// `names` keep a generic title; whether they're offered is up to the handler's can_run().
void set_workspace_switch_titles(ModuleRegistry&, WorkspacesCommandHandler& handler, std::span<std::string const> names);

StringView workspace_switch_action_id(size_t slot);

}
