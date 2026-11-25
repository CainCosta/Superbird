/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Superbird::Workspaces {

using WorkspaceId = int64_t;
// A tab, by its SessionTabId, which stays the same across restarts.
using TabKey = int64_t;
// A window, by its SessionWindowId.
using WindowKey = int64_t;

struct Workspace {
    WorkspaceId id { 0 };
    std::string name;
    // A short glyph shown in the switcher (typically one emoji); empty shows the name's first letter.
    std::string icon;
    // 0xRRGGBB accent tint, if any.
    std::optional<uint32_t> tint;

    bool operator==(Workspace const&) const = default;
};

inline constexpr std::string_view DEFAULT_WORKSPACE_NAME = "Default";
inline constexpr size_t MAXIMUM_WORKSPACE_NAME_LENGTH = 64;

// The workspaces of one window, which tabs belong to which, and which one is showing. Pure data with no Qt, so the
// rules (assignment, fallback on switch and removal, visibility) are unit-tested on their own.
//
// Rules:
// - There is always at least one workspace, and exactly one is active.
// - A tab belongs to exactly one workspace. A new tab joins the active workspace unless told otherwise.
// - Only tabs of the active workspace are visible; pinned tabs are visible everywhere when pinned tabs are global.
// - Switching remembers the tab last active in the workspace being left and comes back to it.
class WorkspaceModel {
public:
    WorkspaceModel();

    std::vector<Workspace> const& workspaces() const { return m_workspaces; }
    Workspace const* workspace(WorkspaceId) const;
    std::optional<size_t> index_of(WorkspaceId) const;
    WorkspaceId active_workspace() const { return m_active; }

    // Creation and editing. Names are trimmed, capped, and default to "Workspace N" when empty.
    WorkspaceId create_workspace(std::string_view name, std::string_view icon = {}, std::optional<uint32_t> tint = {});
    bool rename_workspace(WorkspaceId, std::string_view name);
    bool set_icon(WorkspaceId, std::string_view icon);
    bool set_tint(WorkspaceId, std::optional<uint32_t> tint);
    bool move_workspace(WorkspaceId, size_t new_index);
    // Removes a workspace, moving its tabs into `fallback` (by default the neighbouring workspace). The last workspace
    // can't be removed. If the active one goes, the fallback becomes active.
    bool remove_workspace(WorkspaceId, std::optional<WorkspaceId> fallback = {});

    // Tabs.
    void add_tab(TabKey, std::optional<WorkspaceId> workspace = {});
    void remove_tab(TabKey);
    bool assign_tab(TabKey, WorkspaceId);
    std::optional<WorkspaceId> workspace_of(TabKey) const;
    // The tabs of a workspace, in the given strip order.
    std::vector<TabKey> tabs_in(WorkspaceId, std::span<TabKey const> strip_order) const;
    size_t tab_count(WorkspaceId) const;

    bool pinned_tabs_are_global() const { return m_pinned_tabs_are_global; }
    void set_pinned_tabs_are_global(bool global) { m_pinned_tabs_are_global = global; }
    bool is_visible(TabKey, bool is_pinned) const;

    // Records that a tab became the window's current tab. Returns the workspace to switch to if the tab lives in
    // another workspace (e.g. it was picked from the Command Palette), so the window can follow it.
    std::optional<WorkspaceId> tab_activated(TabKey);

    struct SwitchResult {
        WorkspaceId previous { 0 };
        WorkspaceId current { 0 };
        // The tab to show: the one last active in the target workspace if it's still open, else its first tab in
        // strip order. Nothing means the workspace is empty and the window should open a new tab in it.
        std::optional<TabKey> tab_to_activate;
    };
    // Makes a workspace active. Returns nothing if it doesn't exist or is already active.
    std::optional<SwitchResult> activate(WorkspaceId, std::span<TabKey const> strip_order);
    std::optional<WorkspaceId> next_workspace(int direction) const;

    // Persistence: the whole state, and loading it back with validation (unknown workspaces in assignments are dropped,
    // an unknown active workspace falls back to the first, an empty list gets a default workspace).
    struct Snapshot {
        std::vector<Workspace> workspaces;
        WorkspaceId active { 0 };
        // Sorted by tab.
        std::vector<std::pair<TabKey, WorkspaceId>> assignments;

        bool operator==(Snapshot const&) const = default;
    };
    Snapshot snapshot() const;
    static WorkspaceModel from_snapshot(Snapshot const&);

private:
    WorkspaceId next_id();

    std::vector<Workspace> m_workspaces;
    WorkspaceId m_active { 0 };
    WorkspaceId m_next_id { 1 };
    std::unordered_map<TabKey, WorkspaceId> m_tab_workspace;
    std::unordered_map<WorkspaceId, TabKey> m_last_active_tab;
    bool m_pinned_tabs_are_global { true };
};

// Database rows for one window's workspaces, as WorkspaceStore reads and writes them in Workspaces.db. Converting a
// snapshot to rows and back is lossless, which the tests check; the store itself only moves rows.
struct WorkspaceRow {
    WindowKey window { 0 };
    WorkspaceId id { 0 };
    std::string name;
    std::string icon;
    std::optional<uint32_t> tint;
    int64_t position { 0 };
    bool is_active { false };

    bool operator==(WorkspaceRow const&) const = default;
};

struct TabAssignmentRow {
    TabKey tab { 0 };
    WindowKey window { 0 };
    WorkspaceId workspace { 0 };

    bool operator==(TabAssignmentRow const&) const = default;
};

struct WorkspaceRows {
    std::vector<WorkspaceRow> workspaces;
    std::vector<TabAssignmentRow> assignments;
};

WorkspaceRows snapshot_to_rows(WindowKey, WorkspaceModel::Snapshot const&);
// Builds a snapshot from the rows of one window (rows of other windows are ignored).
WorkspaceModel::Snapshot rows_to_snapshot(WindowKey, WorkspaceRows const&);

}
