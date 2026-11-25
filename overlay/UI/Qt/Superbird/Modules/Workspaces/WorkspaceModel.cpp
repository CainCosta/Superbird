/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceModel.h>

#include <algorithm>

namespace Superbird::Workspaces {

namespace {

// Cuts a UTF-8 string to at most `limit` bytes without splitting a character.
void truncate_utf8(std::string& text, size_t limit)
{
    if (text.size() <= limit)
        return;
    size_t cut = limit;
    // Step back to the first byte of the character that straddles the limit.
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
    text.resize(cut);
}

std::string clean_name(std::string_view name)
{
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!name.empty() && is_space(name.front()))
        name.remove_prefix(1);
    while (!name.empty() && is_space(name.back()))
        name.remove_suffix(1);
    std::string result;
    for (auto c : name)
        result.push_back(is_space(c) ? ' ' : c);
    truncate_utf8(result, MAXIMUM_WORKSPACE_NAME_LENGTH);
    return result;
}

// An icon is a glyph or two (an emoji with modifiers fits), not a label.
std::string clean_icon(std::string_view icon)
{
    std::string result(icon);
    truncate_utf8(result, 16);
    return result;
}

}

WorkspaceModel::WorkspaceModel()
{
    m_active = create_workspace(DEFAULT_WORKSPACE_NAME);
}

WorkspaceId WorkspaceModel::next_id()
{
    return m_next_id++;
}

Workspace const* WorkspaceModel::workspace(WorkspaceId id) const
{
    auto it = std::find_if(m_workspaces.begin(), m_workspaces.end(), [&](auto const& workspace) { return workspace.id == id; });
    return it == m_workspaces.end() ? nullptr : &*it;
}

std::optional<size_t> WorkspaceModel::index_of(WorkspaceId id) const
{
    for (size_t i = 0; i < m_workspaces.size(); ++i) {
        if (m_workspaces[i].id == id)
            return i;
    }
    return {};
}

WorkspaceId WorkspaceModel::create_workspace(std::string_view name, std::string_view icon, std::optional<uint32_t> tint)
{
    Workspace workspace;
    workspace.id = next_id();
    workspace.name = clean_name(name);
    if (workspace.name.empty())
        workspace.name = "Workspace " + std::to_string(m_workspaces.size() + 1);
    workspace.icon = clean_icon(icon);
    if (tint.has_value())
        workspace.tint = *tint & 0xFFFFFF;
    m_workspaces.push_back(std::move(workspace));
    return m_workspaces.back().id;
}

bool WorkspaceModel::rename_workspace(WorkspaceId id, std::string_view name)
{
    auto index = index_of(id);
    auto cleaned = clean_name(name);
    if (!index.has_value() || cleaned.empty())
        return false;
    m_workspaces[*index].name = std::move(cleaned);
    return true;
}

bool WorkspaceModel::set_icon(WorkspaceId id, std::string_view icon)
{
    auto index = index_of(id);
    if (!index.has_value())
        return false;
    m_workspaces[*index].icon = clean_icon(icon);
    return true;
}

bool WorkspaceModel::set_tint(WorkspaceId id, std::optional<uint32_t> tint)
{
    auto index = index_of(id);
    if (!index.has_value())
        return false;
    m_workspaces[*index].tint = tint.has_value() ? std::optional<uint32_t>(*tint & 0xFFFFFF) : std::nullopt;
    return true;
}

bool WorkspaceModel::move_workspace(WorkspaceId id, size_t new_index)
{
    auto index = index_of(id);
    if (!index.has_value())
        return false;
    new_index = std::min(new_index, m_workspaces.size() - 1);
    if (new_index == *index)
        return false;
    auto workspace = std::move(m_workspaces[*index]);
    m_workspaces.erase(m_workspaces.begin() + static_cast<std::ptrdiff_t>(*index));
    m_workspaces.insert(m_workspaces.begin() + static_cast<std::ptrdiff_t>(new_index), std::move(workspace));
    return true;
}

bool WorkspaceModel::remove_workspace(WorkspaceId id, std::optional<WorkspaceId> fallback)
{
    auto index = index_of(id);
    if (!index.has_value() || m_workspaces.size() <= 1)
        return false;
    if (fallback.has_value() && (*fallback == id || !index_of(*fallback).has_value()))
        return false;
    if (!fallback.has_value()) {
        // The workspace before it, or after it when it's the first.
        fallback = m_workspaces[*index == 0 ? 1 : *index - 1].id;
    }

    for (auto& [tab, workspace] : m_tab_workspace) {
        if (workspace == id)
            workspace = *fallback;
    }
    m_last_active_tab.erase(id);
    m_workspaces.erase(m_workspaces.begin() + static_cast<std::ptrdiff_t>(*index));
    if (m_active == id)
        m_active = *fallback;
    return true;
}

void WorkspaceModel::add_tab(TabKey tab, std::optional<WorkspaceId> workspace)
{
    if (m_tab_workspace.contains(tab) && !workspace.has_value())
        return;
    auto target = workspace.has_value() && index_of(*workspace).has_value() ? *workspace : m_active;
    m_tab_workspace[tab] = target;
}

void WorkspaceModel::remove_tab(TabKey tab)
{
    m_tab_workspace.erase(tab);
    for (auto it = m_last_active_tab.begin(); it != m_last_active_tab.end();) {
        if (it->second == tab)
            it = m_last_active_tab.erase(it);
        else
            ++it;
    }
}

bool WorkspaceModel::assign_tab(TabKey tab, WorkspaceId workspace)
{
    if (!index_of(workspace).has_value())
        return false;
    auto it = m_tab_workspace.find(tab);
    if (it != m_tab_workspace.end() && it->second == workspace)
        return false;
    if (it != m_tab_workspace.end()) {
        if (auto last = m_last_active_tab.find(it->second); last != m_last_active_tab.end() && last->second == tab)
            m_last_active_tab.erase(last);
    }
    m_tab_workspace[tab] = workspace;
    return true;
}

std::optional<WorkspaceId> WorkspaceModel::workspace_of(TabKey tab) const
{
    auto it = m_tab_workspace.find(tab);
    if (it == m_tab_workspace.end())
        return {};
    return it->second;
}

std::vector<TabKey> WorkspaceModel::tabs_in(WorkspaceId workspace, std::span<TabKey const> strip_order) const
{
    std::vector<TabKey> tabs;
    for (auto tab : strip_order) {
        if (workspace_of(tab) == workspace)
            tabs.push_back(tab);
    }
    return tabs;
}

size_t WorkspaceModel::tab_count(WorkspaceId workspace) const
{
    return static_cast<size_t>(std::count_if(m_tab_workspace.begin(), m_tab_workspace.end(), [&](auto const& entry) { return entry.second == workspace; }));
}

bool WorkspaceModel::is_visible(TabKey tab, bool is_pinned) const
{
    if (is_pinned && m_pinned_tabs_are_global)
        return true;
    auto workspace = workspace_of(tab);
    // A tab the model hasn't heard of yet shows, rather than vanishing.
    return !workspace.has_value() || *workspace == m_active;
}

std::optional<WorkspaceId> WorkspaceModel::tab_activated(TabKey tab)
{
    auto workspace = workspace_of(tab);
    if (!workspace.has_value())
        return {};
    m_last_active_tab[*workspace] = tab;
    if (*workspace != m_active)
        return *workspace;
    return {};
}

std::optional<WorkspaceModel::SwitchResult> WorkspaceModel::activate(WorkspaceId workspace, std::span<TabKey const> strip_order)
{
    if (workspace == m_active || !index_of(workspace).has_value())
        return {};
    SwitchResult result;
    result.previous = m_active;
    result.current = workspace;
    m_active = workspace;

    if (auto last = m_last_active_tab.find(workspace); last != m_last_active_tab.end()) {
        if (std::find(strip_order.begin(), strip_order.end(), last->second) != strip_order.end() && workspace_of(last->second) == workspace)
            result.tab_to_activate = last->second;
    }
    if (!result.tab_to_activate.has_value()) {
        auto tabs = tabs_in(workspace, strip_order);
        if (!tabs.empty())
            result.tab_to_activate = tabs.front();
    }
    return result;
}

std::optional<WorkspaceId> WorkspaceModel::next_workspace(int direction) const
{
    if (m_workspaces.size() < 2)
        return {};
    auto index = static_cast<int64_t>(index_of(m_active).value_or(0));
    auto count = static_cast<int64_t>(m_workspaces.size());
    auto next = ((index + (direction >= 0 ? 1 : -1)) % count + count) % count;
    return m_workspaces[static_cast<size_t>(next)].id;
}

WorkspaceModel::Snapshot WorkspaceModel::snapshot() const
{
    Snapshot snapshot;
    snapshot.workspaces = m_workspaces;
    snapshot.active = m_active;
    snapshot.assignments.assign(m_tab_workspace.begin(), m_tab_workspace.end());
    std::sort(snapshot.assignments.begin(), snapshot.assignments.end());
    return snapshot;
}

WorkspaceModel WorkspaceModel::from_snapshot(Snapshot const& snapshot)
{
    WorkspaceModel model;
    std::vector<Workspace> workspaces;
    WorkspaceId highest_id = 0;
    for (auto const& workspace : snapshot.workspaces) {
        if (workspace.id <= 0)
            continue;
        if (std::any_of(workspaces.begin(), workspaces.end(), [&](auto const& existing) { return existing.id == workspace.id; }))
            continue;
        auto copy = workspace;
        copy.name = clean_name(copy.name);
        if (copy.name.empty())
            copy.name = "Workspace " + std::to_string(workspaces.size() + 1);
        copy.icon = clean_icon(copy.icon);
        workspaces.push_back(std::move(copy));
        highest_id = std::max(highest_id, workspace.id);
    }
    if (workspaces.empty())
        return model;

    model.m_workspaces = std::move(workspaces);
    model.m_next_id = highest_id + 1;
    model.m_active = model.index_of(snapshot.active).has_value() ? snapshot.active : model.m_workspaces.front().id;
    for (auto const& [tab, workspace] : snapshot.assignments) {
        if (model.index_of(workspace).has_value())
            model.m_tab_workspace[tab] = workspace;
    }
    return model;
}

WorkspaceRows snapshot_to_rows(WindowKey window, WorkspaceModel::Snapshot const& snapshot)
{
    WorkspaceRows rows;
    for (size_t i = 0; i < snapshot.workspaces.size(); ++i) {
        auto const& workspace = snapshot.workspaces[i];
        rows.workspaces.push_back({
            .window = window,
            .id = workspace.id,
            .name = workspace.name,
            .icon = workspace.icon,
            .tint = workspace.tint,
            .position = static_cast<int64_t>(i),
            .is_active = workspace.id == snapshot.active,
        });
    }
    for (auto const& [tab, workspace] : snapshot.assignments)
        rows.assignments.push_back({ .tab = tab, .window = window, .workspace = workspace });
    return rows;
}

WorkspaceModel::Snapshot rows_to_snapshot(WindowKey window, WorkspaceRows const& rows)
{
    WorkspaceModel::Snapshot snapshot;
    std::vector<WorkspaceRow> mine;
    for (auto const& row : rows.workspaces) {
        if (row.window == window)
            mine.push_back(row);
    }
    std::stable_sort(mine.begin(), mine.end(), [](auto const& a, auto const& b) { return a.position < b.position; });
    for (auto const& row : mine) {
        snapshot.workspaces.push_back({ .id = row.id, .name = row.name, .icon = row.icon, .tint = row.tint });
        if (row.is_active)
            snapshot.active = row.id;
    }
    for (auto const& row : rows.assignments) {
        if (row.window == window)
            snapshot.assignments.emplace_back(row.tab, row.workspace);
    }
    std::sort(snapshot.assignments.begin(), snapshot.assignments.end());
    return snapshot;
}

}
