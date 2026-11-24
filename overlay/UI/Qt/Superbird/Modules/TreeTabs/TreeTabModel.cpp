/*
 * Copyright (c) 2026, Sidelady contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StdLibExtras.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabModel.h>

namespace Ladybird {

TreeNode* TreeTabModel::mutable_node(TreeNodeId node_id)
{
    auto it = m_nodes.find(node_id);
    if (it == m_nodes.end())
        return nullptr;
    return &it->value;
}

TreeNode const* TreeTabModel::node(TreeNodeId node_id) const
{
    auto it = m_nodes.find(node_id);
    if (it == m_nodes.end())
        return nullptr;
    return &it->value;
}

Vector<TreeNodeId>& TreeTabModel::sibling_list(Optional<TreeNodeId> parent_id)
{
    if (parent_id.has_value()) {
        if (auto* parent = mutable_node(*parent_id))
            return parent->children;
    }
    return m_roots;
}

Vector<TreeNodeId> const& TreeTabModel::sibling_list(Optional<TreeNodeId> parent_id) const
{
    if (parent_id.has_value()) {
        if (auto const* parent = node(*parent_id))
            return parent->children;
    }
    return m_roots;
}

TreeNodeId TreeTabModel::add_tab(Tab* tab, Optional<SessionTabId> session_tab_id, Optional<TreeNodeId> parent_id, WebView::TreeTabChildPosition child_position)
{
    if (auto existing = node_id_for_tab(tab); existing.has_value())
        return *existing;

    if (parent_id.has_value() && !m_nodes.contains(*parent_id))
        parent_id.clear();

    auto id = m_next_id++;
    TreeNode new_node {
        .id = id,
        .tab = tab,
        .session_tab_id = session_tab_id,
        .parent_id = parent_id,
        .children = {},
        .collapsed = false,
    };
    m_nodes.set(id, move(new_node));
    m_tab_to_node.set(tab, id);

    auto& siblings = sibling_list(parent_id);
    if (parent_id.has_value() && child_position == WebView::TreeTabChildPosition::Top)
        siblings.insert(0, id);
    else
        siblings.append(id);

    return id;
}

void TreeTabModel::replace_tab(Tab const* old_tab, Tab* new_tab)
{
    auto existing = m_tab_to_node.get(old_tab);
    if (!existing.has_value())
        return;

    auto node_id = *existing;
    m_tab_to_node.remove(old_tab);
    m_tab_to_node.set(new_tab, node_id);

    if (auto* entry = mutable_node(node_id))
        entry->tab = new_tab;
}

void TreeTabModel::set_session_tab_id(Tab const* tab, Optional<SessionTabId> session_tab_id)
{
    auto node_id = node_id_for_tab(tab);
    if (!node_id.has_value())
        return;
    if (auto* entry = mutable_node(*node_id))
        entry->session_tab_id = session_tab_id;
}

Optional<TreeNodeId> TreeTabModel::node_id_for_tab(Tab const* tab) const
{
    if (!tab)
        return {};
    return m_tab_to_node.get(tab).copy();
}

Optional<TreeNodeId> TreeTabModel::node_id_for_session_tab_id(SessionTabId session_tab_id) const
{
    for (auto const& [id, entry] : m_nodes) {
        if (entry.session_tab_id == session_tab_id)
            return id;
    }
    return {};
}

bool TreeTabModel::is_ancestor(TreeNodeId potential_ancestor, TreeNodeId node_id) const
{
    Optional<TreeNodeId> cursor = node_id;
    size_t guard = 0;
    while (cursor.has_value() && guard++ <= m_nodes.size()) {
        if (*cursor == potential_ancestor)
            return true;
        auto const* entry = node(*cursor);
        if (!entry)
            break;
        cursor = entry->parent_id;
    }
    return false;
}

size_t TreeTabModel::depth_of(TreeNodeId node_id) const
{
    size_t depth = 0;
    auto const* entry = node(node_id);
    while (entry && entry->parent_id.has_value() && depth <= m_nodes.size()) {
        ++depth;
        entry = node(*entry->parent_id);
    }
    return depth;
}

void TreeTabModel::append_subtree(TreeNodeId node_id, Vector<TreeNodeId>& out) const
{
    auto const* entry = node(node_id);
    if (!entry)
        return;
    out.append(node_id);
    for (auto child_id : entry->children)
        append_subtree(child_id, out);
}

size_t TreeTabModel::descendant_count(TreeNodeId node_id) const
{
    auto const* entry = node(node_id);
    if (!entry)
        return 0;
    size_t total = 0;
    for (auto child_id : entry->children)
        total += 1 + descendant_count(child_id);
    return total;
}

Vector<TreeNodeId> TreeTabModel::subtree_nodes(TreeNodeId node_id) const
{
    Vector<TreeNodeId> nodes;
    append_subtree(node_id, nodes);
    return nodes;
}

Vector<Tab*> TreeTabModel::subtree_tabs(TreeNodeId node_id) const
{
    auto ids = subtree_nodes(node_id);
    Vector<Tab*> tabs;
    tabs.ensure_capacity(ids.size());
    for (auto id : ids) {
        if (auto const* entry = node(id); entry && entry->tab)
            tabs.append(entry->tab);
    }
    return tabs;
}

Vector<Tab*> TreeTabModel::linearized_tabs() const
{
    Vector<TreeNodeId> ids;
    ids.ensure_capacity(m_nodes.size());
    for (auto root_id : m_roots)
        append_subtree(root_id, ids);

    Vector<Tab*> tabs;
    tabs.ensure_capacity(ids.size());
    for (auto id : ids) {
        if (auto const* entry = node(id); entry && entry->tab)
            tabs.append(entry->tab);
    }
    return tabs;
}

void TreeTabModel::collect_descendant_indicators(
    TreeNodeId node_id,
    Tab const* active_tab,
    Function<bool(Tab const&)> const& has_audio,
    Function<bool(Tab const&)> const& needs_attention,
    size_t& count,
    bool& has_active,
    bool& has_audio_flag,
    bool& has_attention_flag) const
{
    auto const* entry = node(node_id);
    if (!entry)
        return;

    for (auto child_id : entry->children) {
        auto const* child = node(child_id);
        if (!child)
            continue;
        ++count;
        if (child->tab) {
            if (child->tab == active_tab)
                has_active = true;
            if (has_audio && has_audio(*child->tab))
                has_audio_flag = true;
            if (needs_attention && needs_attention(*child->tab))
                has_attention_flag = true;
        }
        collect_descendant_indicators(child_id, active_tab, has_audio, needs_attention, count, has_active, has_audio_flag, has_attention_flag);
    }
}

void TreeTabModel::append_visible_rows(
    TreeNodeId node_id,
    size_t depth,
    u32 guide_mask,
    bool is_last_sibling,
    Tab const* active_tab,
    Function<bool(Tab const&)> const& has_audio,
    Function<bool(Tab const&)> const& needs_attention,
    Vector<VisibleTreeRow>& out) const
{
    auto const* entry = node(node_id);
    if (!entry)
        return;

    bool const has_children = !entry->children.is_empty();
    bool const effective_collapsed = has_children && entry->collapsed;

    size_t desc_count = 0;
    bool has_active_desc = false;
    bool has_audio_desc = false;
    bool has_attention_desc = false;
    if (effective_collapsed) {
        collect_descendant_indicators(node_id, active_tab, has_audio, needs_attention, desc_count, has_active_desc, has_audio_desc, has_attention_desc);
    }

    out.append(VisibleTreeRow {
        .node_id = node_id,
        .tab = entry->tab,
        .depth = depth,
        .has_children = has_children,
        .collapsed = effective_collapsed,
        .descendant_count = desc_count,
        .is_selected = m_selected_nodes.contains(node_id),
        .is_last_sibling = is_last_sibling,
        .guide_mask = guide_mask,
        .has_active_descendant = has_active_desc,
        .has_audio_descendant = has_audio_desc,
        .has_attention_descendant = has_attention_desc,
    });

    if (effective_collapsed)
        return;

    u32 child_guide_mask = guide_mask;
    if (depth < 31 && !is_last_sibling)
        child_guide_mask |= (1u << depth);

    for (size_t i = 0; i < entry->children.size(); ++i) {
        bool child_is_last = (i + 1 == entry->children.size());
        append_visible_rows(entry->children[i], depth + 1, child_guide_mask, child_is_last, active_tab, has_audio, needs_attention, out);
    }
}

Vector<VisibleTreeRow> TreeTabModel::visible_rows(
    Tab const* active_tab,
    Function<bool(Tab const&)> const& has_audio,
    Function<bool(Tab const&)> const& needs_attention) const
{
    Vector<VisibleTreeRow> rows;
    rows.ensure_capacity(m_nodes.size());
    for (size_t i = 0; i < m_roots.size(); ++i) {
        bool is_last = (i + 1 == m_roots.size());
        append_visible_rows(m_roots[i], 0, 0, is_last, active_tab, has_audio, needs_attention, rows);
    }
    return rows;
}

bool TreeTabModel::set_collapsed(TreeNodeId node_id, bool collapsed, bool auto_collapse_siblings)
{
    auto* entry = mutable_node(node_id);
    if (!entry || entry->children.is_empty())
        return false;

    bool changed = false;
    if (entry->collapsed != collapsed) {
        entry->collapsed = collapsed;
        changed = true;
    }

    if (!collapsed && auto_collapse_siblings) {
        auto parent_id = entry->parent_id;
        auto siblings = sibling_list(parent_id);
        for (auto sibling_id : siblings) {
            if (sibling_id == node_id)
                continue;
            if (auto* sibling = mutable_node(sibling_id); sibling && !sibling->children.is_empty() && !sibling->collapsed) {
                sibling->collapsed = true;
                changed = true;
            }
        }
    }

    return changed;
}

bool TreeTabModel::toggle_collapsed(TreeNodeId node_id, bool auto_collapse_siblings)
{
    auto const* entry = node(node_id);
    if (!entry || entry->children.is_empty())
        return false;
    return set_collapsed(node_id, !entry->collapsed, auto_collapse_siblings);
}

bool TreeTabModel::reveal_node(TreeNodeId node_id)
{
    auto const* entry = node(node_id);
    if (!entry)
        return false;

    bool changed = false;
    auto cursor = entry->parent_id;
    size_t guard = 0;
    while (cursor.has_value() && guard++ <= m_nodes.size()) {
        auto* parent = mutable_node(*cursor);
        if (!parent)
            break;
        if (parent->collapsed) {
            parent->collapsed = false;
            changed = true;
        }
        cursor = parent->parent_id;
    }
    return changed;
}

void TreeTabModel::detach_from_parent(TreeNodeId node_id)
{
    auto* entry = mutable_node(node_id);
    if (!entry)
        return;
    auto& siblings = sibling_list(entry->parent_id);
    if (auto index = siblings.find_first_index(node_id); index.has_value())
        siblings.remove(*index);
    entry->parent_id.clear();
}

bool TreeTabModel::move_node(TreeNodeId node_id, Optional<TreeNodeId> new_parent_id, size_t insertion_index, bool include_subtree)
{
    auto* entry = mutable_node(node_id);
    if (!entry)
        return false;

    if (new_parent_id.has_value()) {
        if (!m_nodes.contains(*new_parent_id))
            return false;
        if (include_subtree && is_ancestor(node_id, *new_parent_id))
            return false;
        if (!include_subtree && *new_parent_id == node_id)
            return false;
    }

    auto old_parent_id = entry->parent_id;

    // If moving a single tab without its subtree, lift its children into its previous position first.
    if (!include_subtree && !entry->children.is_empty()) {
        auto children_copy = AK::move(entry->children);
        entry->children.clear();
        entry->collapsed = false;

        auto& old_siblings = sibling_list(old_parent_id);
        auto old_index = old_siblings.find_first_index(node_id).value_or(old_siblings.size());
        old_siblings.remove(old_index);

        for (size_t i = 0; i < children_copy.size(); ++i) {
            auto child_id = children_copy[i];
            if (auto* child = mutable_node(child_id))
                child->parent_id = old_parent_id;
            old_siblings.insert(old_index + i, child_id);
        }

        // If moving within the same sibling list after removing node_id and inserting children, clamp safely.
        auto& target_siblings = sibling_list(new_parent_id);
        if (old_parent_id == new_parent_id && insertion_index > old_index)
            insertion_index = min(target_siblings.size(), insertion_index - 1 + children_copy.size());
        else
            insertion_index = min(insertion_index, target_siblings.size());

        entry->parent_id = new_parent_id;
        target_siblings.insert(insertion_index, node_id);
        return true;
    }

    auto& old_siblings = sibling_list(old_parent_id);
    auto old_index = old_siblings.find_first_index(node_id);
    if (!old_index.has_value())
        return false;

    if (old_parent_id == new_parent_id && (*old_index == insertion_index || *old_index + 1 == insertion_index))
        return false;

    old_siblings.remove(*old_index);
    if (old_parent_id == new_parent_id && insertion_index > *old_index)
        --insertion_index;

    auto& target_siblings = sibling_list(new_parent_id);
    insertion_index = min(insertion_index, target_siblings.size());
    entry->parent_id = new_parent_id;
    target_siblings.insert(insertion_index, node_id);
    return true;
}

bool TreeTabModel::drop_node(TreeNodeId dragged_node_id, TreeNodeId target_node_id, TreeDropPosition position, bool include_subtree)
{
    if (dragged_node_id == target_node_id)
        return false;
    auto const* target = node(target_node_id);
    if (!target)
        return false;

    switch (position) {
    case TreeDropPosition::Inside: {
        auto target_children_size = target->children.size();
        if (auto* mutable_target = mutable_node(target_node_id))
            mutable_target->collapsed = false;
        return move_node(dragged_node_id, target_node_id, target_children_size, include_subtree);
    }
    case TreeDropPosition::Above: {
        auto parent_id = target->parent_id;
        auto const& siblings = sibling_list(parent_id);
        auto target_index = siblings.find_first_index(target_node_id).value_or(0);
        return move_node(dragged_node_id, parent_id, target_index, include_subtree);
    }
    case TreeDropPosition::Below: {
        // Dropping below an expanded parent inserts as its first child (matches TST/Sidebery visual flow).
        if (!target->children.is_empty() && !target->collapsed)
            return move_node(dragged_node_id, target_node_id, 0, include_subtree);
        auto parent_id = target->parent_id;
        auto const& siblings = sibling_list(parent_id);
        auto target_index = siblings.find_first_index(target_node_id).value_or(siblings.size());
        return move_node(dragged_node_id, parent_id, target_index + 1, include_subtree);
    }
    }
    VERIFY_NOT_REACHED();
}

bool TreeTabModel::indent_node(TreeNodeId node_id)
{
    auto const* entry = node(node_id);
    if (!entry)
        return false;
    auto parent_id = entry->parent_id;
    auto const& siblings = sibling_list(parent_id);
    auto index = siblings.find_first_index(node_id);
    if (!index.has_value() || *index == 0)
        return false;

    auto new_parent_id = siblings[*index - 1];
    auto* new_parent = mutable_node(new_parent_id);
    if (!new_parent)
        return false;
    new_parent->collapsed = false;
    return move_node(node_id, new_parent_id, new_parent->children.size(), true);
}

bool TreeTabModel::outdent_node(TreeNodeId node_id)
{
    auto const* entry = node(node_id);
    if (!entry || !entry->parent_id.has_value())
        return false;

    auto parent_id = *entry->parent_id;
    auto const* parent = node(parent_id);
    if (!parent)
        return false;

    auto grandparent_id = parent->parent_id;
    auto const& parent_siblings = sibling_list(grandparent_id);
    auto parent_index = parent_siblings.find_first_index(parent_id);
    if (!parent_index.has_value())
        return false;

    return move_node(node_id, grandparent_id, *parent_index + 1, true);
}

void TreeTabModel::remove_node(TreeNodeId node_id, WebView::TreeTabCloseBehavior close_behavior)
{
    auto it = m_nodes.find(node_id);
    if (it == m_nodes.end())
        return;

    auto parent_id = it->value.parent_id;
    auto children = AK::move(it->value.children);
    auto* tab = it->value.tab;

    auto& siblings = sibling_list(parent_id);
    auto old_index = siblings.find_first_index(node_id).value_or(siblings.size());
    if (old_index < siblings.size())
        siblings.remove(old_index);

    m_tab_to_node.remove(tab);
    m_selected_nodes.remove(node_id);
    if (m_selection_anchor == node_id)
        m_selection_anchor.clear();
    m_nodes.remove(node_id);

    if (children.is_empty())
        return;

    if (close_behavior == WebView::TreeTabCloseBehavior::PromoteFirstChild) {
        auto promoted_id = children[0];
        if (auto* promoted = mutable_node(promoted_id)) {
            promoted->parent_id = parent_id;
            sibling_list(parent_id).insert(min(old_index, sibling_list(parent_id).size()), promoted_id);

            // Attach remaining siblings as children at the top of the promoted child so order is preserved.
            for (size_t i = 1; i < children.size(); ++i) {
                auto sibling_child_id = children[i];
                if (auto* sibling_child = mutable_node(sibling_child_id)) {
                    sibling_child->parent_id = promoted_id;
                    promoted->children.insert(i - 1, sibling_child_id);
                }
            }
        }
        return;
    }

    // AttachToGrandparent (also fallback if CloseBranch callers remove nodes one-by-one).
    auto& target_siblings = sibling_list(parent_id);
    auto insert_at = min(old_index, target_siblings.size());
    for (size_t i = 0; i < children.size(); ++i) {
        auto child_id = children[i];
        if (auto* child = mutable_node(child_id))
            child->parent_id = parent_id;
        target_siblings.insert(insert_at + i, child_id);
    }
}

void TreeTabModel::apply_persisted_state(HashMap<SessionTabId, PersistedTreeNodeState> const& persisted_by_tab)
{
    if (persisted_by_tab.is_empty() || m_nodes.is_empty())
        return;

    HashMap<SessionTabId, TreeNodeId> session_to_node;
    for (auto const& [id, entry] : m_nodes) {
        if (entry.session_tab_id.has_value())
            session_to_node.set(*entry.session_tab_id, id);
    }

    // Walk current linearized order so relative sibling order is preserved while rebuilding parent links.
    auto current_tabs = linearized_tabs();
    Vector<TreeNodeId> ordered_ids;
    ordered_ids.ensure_capacity(current_tabs.size());
    for (auto* tab : current_tabs) {
        if (auto id = node_id_for_tab(tab); id.has_value())
            ordered_ids.append(*id);
    }

    m_roots.clear();
    for (auto& [id, entry] : m_nodes) {
        entry.parent_id.clear();
        entry.children.clear();
        entry.collapsed = false;
    }

    for (auto id : ordered_ids) {
        auto* entry = mutable_node(id);
        if (!entry)
            continue;

        Optional<TreeNodeId> restored_parent;
        bool restored_collapsed = false;
        if (entry->session_tab_id.has_value()) {
            if (auto saved = persisted_by_tab.get(*entry->session_tab_id); saved.has_value()) {
                restored_collapsed = saved->collapsed;
                if (saved->parent_session_tab_id.has_value()) {
                    if (auto parent_node = session_to_node.get(*saved->parent_session_tab_id); parent_node.has_value() && *parent_node != id && !is_ancestor(id, *parent_node))
                        restored_parent = *parent_node;
                }
            }
        }

        entry->parent_id = restored_parent;
        entry->collapsed = restored_collapsed;
        sibling_list(restored_parent).append(id);
    }

    // Leaf nodes can never remain collapsed.
    for (auto& [id, entry] : m_nodes) {
        if (entry.children.is_empty())
            entry.collapsed = false;
    }
}

Vector<PersistedTreeNodeState> TreeTabModel::snapshot_persisted_state() const
{
    Vector<PersistedTreeNodeState> state;
    state.ensure_capacity(m_nodes.size());
    for (auto const& [id, entry] : m_nodes) {
        if (!entry.session_tab_id.has_value())
            continue;

        Optional<SessionTabId> parent_session_id;
        if (entry.parent_id.has_value()) {
            if (auto const* parent = node(*entry.parent_id))
                parent_session_id = parent->session_tab_id;
        }
        state.append(PersistedTreeNodeState {
            .session_tab_id = *entry.session_tab_id,
            .parent_session_tab_id = parent_session_id,
            .collapsed = entry.collapsed && !entry.children.is_empty(),
        });
    }
    return state;
}

void TreeTabModel::reconcile_with_unpinned_tabs(ReadonlySpan<Tab*> unpinned_tabs)
{
    HashTable<Tab const*> live_tabs;
    for (auto* tab : unpinned_tabs)
        live_tabs.set(tab);

    Vector<TreeNodeId> stale_ids;
    for (auto const& [id, entry] : m_nodes) {
        if (!live_tabs.contains(entry.tab))
            stale_ids.append(id);
    }
    for (auto stale_id : stale_ids)
        remove_node(stale_id, WebView::TreeTabCloseBehavior::AttachToGrandparent);

    for (auto* tab : unpinned_tabs) {
        if (!m_tab_to_node.contains(tab))
            add_tab(tab, {});
    }
}

void TreeTabModel::clear_selection()
{
    m_selected_nodes.clear();
    m_selection_anchor.clear();
}

void TreeTabModel::select_only(TreeNodeId node_id)
{
    m_selected_nodes.clear();
    if (m_nodes.contains(node_id)) {
        m_selected_nodes.set(node_id);
        m_selection_anchor = node_id;
    } else {
        m_selection_anchor.clear();
    }
}

void TreeTabModel::toggle_selection(TreeNodeId node_id)
{
    if (!m_nodes.contains(node_id))
        return;
    if (m_selected_nodes.contains(node_id))
        m_selected_nodes.remove(node_id);
    else
        m_selected_nodes.set(node_id);
    m_selection_anchor = node_id;
}

void TreeTabModel::select_range(TreeNodeId from_node_id, TreeNodeId to_node_id, Vector<VisibleTreeRow> const& visible)
{
    Optional<size_t> from_index;
    Optional<size_t> to_index;
    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i].node_id == from_node_id)
            from_index = i;
        if (visible[i].node_id == to_node_id)
            to_index = i;
    }

    if (!from_index.has_value() || !to_index.has_value()) {
        select_only(to_node_id);
        return;
    }

    auto start = min(*from_index, *to_index);
    auto end = max(*from_index, *to_index);
    m_selected_nodes.clear();
    for (size_t i = start; i <= end; ++i)
        m_selected_nodes.set(visible[i].node_id);
    m_selection_anchor = from_node_id;
}

Vector<TreeNodeId> TreeTabModel::selected_nodes_in_order() const
{
    Vector<TreeNodeId> ordered;
    if (m_selected_nodes.is_empty())
        return ordered;

    Vector<TreeNodeId> all;
    all.ensure_capacity(m_nodes.size());
    for (auto root_id : m_roots)
        append_subtree(root_id, all);

    for (auto id : all) {
        if (m_selected_nodes.contains(id))
            ordered.append(id);
    }
    return ordered;
}

}
