/*
 * Copyright (c) 2026, Sidelady contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/HashTable.h>
#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/Types.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <LibWebView/Settings.h>
#include <UI/Qt/SessionStore.h>

namespace Ladybird {

class Tab;

using TreeNodeId = u64;

enum class TreeDropPosition : u8 {
    Above,
    Inside,
    Below,
};

// A subtle color tag on a branch: a tab's tag colors its whole subtree, unless a descendant has a tag of its own.
// Stored in TreeTabs.db as its integer value, so values only ever get added.
enum class BranchColor : u8 {
    None = 0,
    Red,
    Orange,
    Yellow,
    Green,
    Teal,
    Blue,
    Purple,
    Pink,
};
inline constexpr u8 BRANCH_COLOR_COUNT = 9;
// A stored value from a newer or corrupt database reads as no color.
BranchColor branch_color_from_storage(i64);

struct TreeNode {
    TreeNodeId id { 0 };
    Tab* tab { nullptr };
    Optional<SessionTabId> session_tab_id;
    Optional<TreeNodeId> parent_id;
    Vector<TreeNodeId> children;
    bool collapsed { false };
    BranchColor color { BranchColor::None };
};

struct VisibleTreeRow {
    TreeNodeId node_id { 0 };
    Tab* tab { nullptr };
    size_t depth { 0 };
    bool has_children { false };
    bool collapsed { false };
    size_t descendant_count { 0 };
    bool is_selected { false };
    bool is_last_sibling { false };
    // Bit d is set when the ancestor at depth d has a following sibling, so vertical tree guides stay connected.
    u32 guide_mask { 0 };
    // Roll-up indicators for collapsed branches so hidden tab state remains visible.
    bool has_active_descendant { false };
    bool has_audio_descendant { false };
    bool has_attention_descendant { false };
    // The branch color the row shows (its own tag, or its nearest tagged ancestor's).
    BranchColor color { BranchColor::None };
};

struct PersistedTreeNodeState {
    SessionTabId session_tab_id { 0 };
    Optional<SessionTabId> parent_session_tab_id;
    bool collapsed { false };
    BranchColor color { BranchColor::None };
};

// Pure tree topology and selection model for unpinned tabs in one BrowserWindow.
// Pinned tabs are always kept in [0, pinned_tab_count()) outside this tree to preserve Ladybird's pinned-prefix invariant.
class TreeTabModel {
    AK_MAKE_NONCOPYABLE(TreeTabModel);

public:
    AK_ALLOC_WITH_KMALLOC;

    TreeTabModel() = default;

    TreeNodeId add_tab(Tab* tab, Optional<SessionTabId> session_tab_id, Optional<TreeNodeId> parent_id = {}, WebView::TreeTabChildPosition child_position = WebView::TreeTabChildPosition::Bottom);
    void replace_tab(Tab const* old_tab, Tab* new_tab);
    void set_session_tab_id(Tab const* tab, Optional<SessionTabId> session_tab_id);

    Optional<TreeNodeId> node_id_for_tab(Tab const* tab) const;
    Optional<TreeNodeId> node_id_for_session_tab_id(SessionTabId session_tab_id) const;
    TreeNode const* node(TreeNodeId node_id) const;

    Vector<TreeNodeId> const& root_ids() const { return m_roots; }
    size_t node_count() const { return m_nodes.size(); }
    bool is_empty() const { return m_nodes.is_empty(); }

    bool is_ancestor(TreeNodeId potential_ancestor, TreeNodeId node_id) const;
    size_t depth_of(TreeNodeId node_id) const;
    size_t descendant_count(TreeNodeId node_id) const;
    Vector<TreeNodeId> subtree_nodes(TreeNodeId node_id) const;
    Vector<Tab*> subtree_tabs(TreeNodeId node_id) const;

    // Canonical flat tab strip order (pre-order DFS of all root trees).
    Vector<Tab*> linearized_tabs() const;

    Vector<VisibleTreeRow> visible_rows(
        Tab const* active_tab,
        Function<bool(Tab const&)> const& has_audio = {},
        Function<bool(Tab const&)> const& needs_attention = {}) const;

    // The rows to show while the sidebar's quick filter is on: every tab that matches, under its ancestors for context.
    // Branches open up to show their matches, whatever their collapsed state.
    Vector<VisibleTreeRow> filtered_rows(Function<bool(Tab const&)> const& matches) const;

    bool set_branch_color(TreeNodeId node_id, BranchColor color);
    BranchColor branch_color(TreeNodeId node_id) const;
    BranchColor effective_branch_color(TreeNodeId node_id) const;

    bool set_collapsed(TreeNodeId node_id, bool collapsed, bool auto_collapse_siblings = false);
    bool toggle_collapsed(TreeNodeId node_id, bool auto_collapse_siblings = false);
    bool reveal_node(TreeNodeId node_id);

    bool move_node(TreeNodeId node_id, Optional<TreeNodeId> new_parent_id, size_t insertion_index, bool include_subtree = true);
    bool drop_node(TreeNodeId dragged_node_id, TreeNodeId target_node_id, TreeDropPosition position, bool include_subtree = true);
    bool indent_node(TreeNodeId node_id);
    bool outdent_node(TreeNodeId node_id);

    // Removes a single node and promotes/reparents its children according to close_behavior.
    // For TreeTabCloseBehavior::CloseBranch, callers should inspect subtree_tabs() first and close descendants cleanly.
    void remove_node(TreeNodeId node_id, WebView::TreeTabCloseBehavior close_behavior);

    // Restores saved parent-child relationships and collapsed flags among currently present tabs.
    void apply_persisted_state(HashMap<SessionTabId, PersistedTreeNodeState> const& persisted_by_tab);
    Vector<PersistedTreeNodeState> snapshot_persisted_state() const;

    // Ensures every unpinned tab is present in the model and removes stale entries.
    void reconcile_with_unpinned_tabs(ReadonlySpan<Tab*> unpinned_tabs);

    // Multi-selection state (always references valid node IDs).
    void clear_selection();
    void select_only(TreeNodeId node_id);
    void toggle_selection(TreeNodeId node_id);
    void select_range(TreeNodeId from_node_id, TreeNodeId to_node_id, Vector<VisibleTreeRow> const& visible);
    bool is_selected(TreeNodeId node_id) const { return m_selected_nodes.contains(node_id); }
    size_t selected_count() const { return m_selected_nodes.size(); }
    Optional<TreeNodeId> selection_anchor() const { return m_selection_anchor; }
    Vector<TreeNodeId> selected_nodes_in_order() const;

private:
    TreeNode* mutable_node(TreeNodeId node_id);
    Vector<TreeNodeId>& sibling_list(Optional<TreeNodeId> parent_id);
    Vector<TreeNodeId> const& sibling_list(Optional<TreeNodeId> parent_id) const;
    void detach_from_parent(TreeNodeId node_id);
    void append_subtree(TreeNodeId node_id, Vector<TreeNodeId>& out) const;
    void append_visible_rows(
        TreeNodeId node_id,
        size_t depth,
        u32 guide_mask,
        bool is_last_sibling,
        Tab const* active_tab,
        Function<bool(Tab const&)> const& has_audio,
        Function<bool(Tab const&)> const& needs_attention,
        Vector<VisibleTreeRow>& out) const;
    bool collect_filter_matches(TreeNodeId node_id, Function<bool(Tab const&)> const& matches, HashTable<TreeNodeId>& included) const;
    void append_filtered_rows(
        TreeNodeId node_id,
        size_t depth,
        u32 guide_mask,
        bool is_last_sibling,
        HashTable<TreeNodeId> const& included,
        Vector<VisibleTreeRow>& out) const;
    void collect_descendant_indicators(
        TreeNodeId node_id,
        Tab const* active_tab,
        Function<bool(Tab const&)> const& has_audio,
        Function<bool(Tab const&)> const& needs_attention,
        size_t& count,
        bool& has_active,
        bool& has_audio_flag,
        bool& has_attention_flag) const;

    TreeNodeId m_next_id { 1 };
    HashMap<TreeNodeId, TreeNode> m_nodes;
    HashMap<Tab const*, TreeNodeId> m_tab_to_node;
    Vector<TreeNodeId> m_roots;

    HashTable<TreeNodeId> m_selected_nodes;
    Optional<TreeNodeId> m_selection_anchor;
};

}
