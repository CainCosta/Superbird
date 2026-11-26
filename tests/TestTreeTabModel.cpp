/*
 * Copyright (c) 2026, Sidelady contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <cstdio>
#include <AK/Vector.h>
#include <AK/HashMap.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabFilter.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabModel.h>

#include <cstdlib>

namespace Ladybird {
class Tab {
public:
    explicit Tab(int id)
        : id(id)
    {
    }
    int id { 0 };
};
}

using namespace Ladybird;

#define EXPECT(cond)                                                                      \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

static Vector<int> ids_of(Vector<Tab*> const& tabs)
{
    Vector<int> out;
    for (auto* tab : tabs)
        out.append(tab->id);
    return out;
}

static Vector<int> row_ids_of(Vector<VisibleTreeRow> const& rows)
{
    Vector<int> out;
    for (auto const& row : rows)
        out.append(row.tab->id);
    return out;
}

static void test_opener_nesting_and_linearization()
{
    Tab t1(1), t2(2), t3(3), t4(4), t5(5);
    TreeTabModel model;

    auto n1 = model.add_tab(&t1, 101);
    auto n2 = model.add_tab(&t2, 102, n1, WebView::TreeTabChildPosition::Bottom);
    auto n3 = model.add_tab(&t3, 103, n1, WebView::TreeTabChildPosition::Bottom);
    (void)model.add_tab(&t4, 104, n2, WebView::TreeTabChildPosition::Bottom);
    (void)model.add_tab(&t5, 105, n1, WebView::TreeTabChildPosition::Top);

    // Expected tree:
    // 1
    //  ├─ 5 (inserted at Top)
    //  ├─ 2
    //  │   └─ 4
    //  └─ 3
    auto flat = ids_of(model.linearized_tabs());
    EXPECT(flat.size() == 5);
    EXPECT(flat[0] == 1 && flat[1] == 5 && flat[2] == 2 && flat[3] == 4 && flat[4] == 3);
    EXPECT(model.descendant_count(n1) == 4);
    EXPECT(model.depth_of(n3) == 1);
    EXPECT(model.is_ancestor(n1, n2));
    EXPECT(!model.is_ancestor(n2, n1));
}

static void test_collapse_rollup_and_reveal()
{
    Tab t1(1), t2(2), t3(3), t4(4);
    TreeTabModel model;

    auto n1 = model.add_tab(&t1, 1);
    auto n2 = model.add_tab(&t2, 2, n1);
    auto n3 = model.add_tab(&t3, 3, n2);
    auto n4 = model.add_tab(&t4, 4);
    auto n4_child = model.add_tab(new Tab(5), 5, n4);
    (void)n4_child;

    // Collapse n1: hides t2 & t3, rolls up active/audio/attention from t3
    EXPECT(model.set_collapsed(n1, true));
    auto rows = model.visible_rows(
        &t3,
        [](Tab const& t) { return t.id == 3; },
        [](Tab const& t) { return t.id == 2; });
    auto visible_ids = row_ids_of(rows);
    EXPECT(visible_ids.size() == 3); // t1, t4, t5
    EXPECT(visible_ids[0] == 1);
    EXPECT(rows[0].collapsed);
    EXPECT(rows[0].descendant_count == 2);
    EXPECT(rows[0].has_active_descendant);
    EXPECT(rows[0].has_audio_descendant);
    EXPECT(rows[0].has_attention_descendant);

    // Expanding n1 with auto_collapse_siblings collapses sibling n4
    EXPECT(model.set_collapsed(n1, false, true));
    EXPECT(!model.node(n1)->collapsed);
    EXPECT(model.node(n4)->collapsed);

    // Reveal expands collapsed ancestors
    EXPECT(model.set_collapsed(n1, true));
    EXPECT(model.set_collapsed(n2, true));
    EXPECT(model.reveal_node(n3));
    EXPECT(!model.node(n1)->collapsed);
    EXPECT(!model.node(n2)->collapsed);
}

static void test_drag_drop_indent_outdent_and_cycle_guard()
{
    Tab t1(1), t2(2), t3(3), t4(4);
    TreeTabModel model;

    auto n1 = model.add_tab(&t1, 1);
    auto n2 = model.add_tab(&t2, 2, n1);
    auto n3 = model.add_tab(&t3, 3);
    auto n4 = model.add_tab(&t4, 4);

    // Cannot drop ancestor inside its own descendant
    EXPECT(!model.drop_node(n1, n2, TreeDropPosition::Inside, true));

    // Drop n3 Inside n1 -> attaches at end of n1's children
    EXPECT(model.drop_node(n3, n1, TreeDropPosition::Inside, true));
    auto flat = ids_of(model.linearized_tabs());
    EXPECT(flat[0] == 1 && flat[1] == 2 && flat[2] == 3 && flat[3] == 4);

    // Drop n4 Below expanded parent n1 -> inserts as FIRST child of n1
    EXPECT(model.drop_node(n4, n1, TreeDropPosition::Below, true));
    flat = ids_of(model.linearized_tabs());
    EXPECT(flat[0] == 1 && flat[1] == 4 && flat[2] == 2 && flat[3] == 3);

    // Indent n2 under previous sibling n4, then outdent back
    EXPECT(model.indent_node(n2));
    EXPECT(model.node(n2)->parent_id == n4);
    EXPECT(model.outdent_node(n2));
    EXPECT(model.node(n2)->parent_id == n1);

    // Move single node (include_subtree = false) lifts children first
    EXPECT(model.drop_node(n2, n4, TreeDropPosition::Inside, true));
    EXPECT(model.drop_node(n4, n3, TreeDropPosition::Below, false));
    // n2 should have been lifted to n1, and n4 moved after n3
    EXPECT(model.node(n2)->parent_id == n1);
    EXPECT(model.node(n4)->children.is_empty());
}

static void test_close_orphan_policies()
{
    // PromoteFirstChild
    {
        Tab t1(1), t2(2), t3(3), t4(4);
        TreeTabModel model;
        auto n1 = model.add_tab(&t1, 1);
        auto n2 = model.add_tab(&t2, 2, n1);
        auto n3 = model.add_tab(&t3, 3, n1);
        (void)model.add_tab(&t4, 4, n2);

        model.remove_node(n1, WebView::TreeTabCloseBehavior::PromoteFirstChild);
        // n2 promoted to root; n3 becomes child of n2 (before n4)
        EXPECT(!model.node(n2)->parent_id.has_value());
        EXPECT(model.node(n3)->parent_id == n2);
        auto flat = ids_of(model.linearized_tabs());
        EXPECT(flat.size() == 3 && flat[0] == 2 && flat[1] == 3 && flat[2] == 4);
    }

    // AttachToGrandparent
    {
        Tab t1(1), t2(2), t3(3);
        TreeTabModel model;
        auto n1 = model.add_tab(&t1, 1);
        auto n2 = model.add_tab(&t2, 2, n1);
        auto n3 = model.add_tab(&t3, 3, n1);

        model.remove_node(n1, WebView::TreeTabCloseBehavior::AttachToGrandparent);
        EXPECT(!model.node(n2)->parent_id.has_value());
        EXPECT(!model.node(n3)->parent_id.has_value());
        auto flat = ids_of(model.linearized_tabs());
        EXPECT(flat.size() == 2 && flat[0] == 2 && flat[1] == 3);
    }
}

static void test_replace_tab_and_persistence_roundtrip()
{
    Tab t1(1), t2(2), t2_discarded(20), t3(3);
    TreeTabModel model;
    auto n1 = model.add_tab(&t1, 10);
    auto n2 = model.add_tab(&t2, 20, n1);
    (void)model.add_tab(&t3, 30, n2);
    model.set_collapsed(n2, true);

    // Simulate Ladybird discarding t2 and swapping in t2_discarded
    model.replace_tab(&t2, &t2_discarded);
    EXPECT(!model.node_id_for_tab(&t2).has_value());
    EXPECT(model.node_id_for_tab(&t2_discarded) == n2);
    EXPECT(model.node(n2)->tab == &t2_discarded);

    // Persist snapshot and restore into a brand-new model with flat tabs
    auto snapshot = model.snapshot_persisted_state();
    HashMap<SessionTabId, PersistedTreeNodeState> saved;
    for (auto const& item : snapshot)
        saved.set(item.session_tab_id, item);

    TreeTabModel restored;
    restored.add_tab(&t1, 10);
    restored.add_tab(&t2_discarded, 20);
    restored.add_tab(&t3, 30);
    restored.apply_persisted_state(saved);

    auto r1 = restored.node_id_for_tab(&t1).value();
    auto r2 = restored.node_id_for_tab(&t2_discarded).value();
    auto r3 = restored.node_id_for_tab(&t3).value();
    EXPECT(!restored.node(r1)->parent_id.has_value());
    EXPECT(restored.node(r2)->parent_id == r1);
    EXPECT(restored.node(r3)->parent_id == r2);
    EXPECT(restored.node(r2)->collapsed);
}

static void test_multi_selection()
{
    Tab t1(1), t2(2), t3(3), t4(4);
    TreeTabModel model;
    auto n1 = model.add_tab(&t1, 1);
    auto n2 = model.add_tab(&t2, 2, n1);
    auto n3 = model.add_tab(&t3, 3, n1);
    auto n4 = model.add_tab(&t4, 4);

    auto rows = model.visible_rows(&t1);
    model.select_only(n2);
    model.select_range(n2, n4, rows);
    auto selected = model.selected_nodes_in_order();
    EXPECT(selected.size() == 3);
    EXPECT(selected[0] == n2 && selected[1] == n3 && selected[2] == n4);

    model.toggle_selection(n3);
    EXPECT(!model.is_selected(n3));
    EXPECT(model.selected_count() == 2);
}

static void test_branch_color_tags_and_persistence()
{
    Tab t1(1), t2(2), t3(3), t4(4);
    TreeTabModel model;
    auto n1 = model.add_tab(&t1, 201);
    auto n2 = model.add_tab(&t2, 202, n1);
    auto n3 = model.add_tab(&t3, 203, n2);
    auto n4 = model.add_tab(&t4, 204);

    EXPECT(model.effective_branch_color(n3) == BranchColor::None);
    EXPECT(model.set_branch_color(n1, BranchColor::Blue));
    EXPECT(!model.set_branch_color(n1, BranchColor::Blue));
    EXPECT(!model.set_branch_color(9999, BranchColor::Red));
    // A tag colors the whole branch...
    EXPECT(model.effective_branch_color(n2) == BranchColor::Blue && model.effective_branch_color(n3) == BranchColor::Blue);
    EXPECT(model.branch_color(n2) == BranchColor::None);
    // ...unless a descendant has its own.
    EXPECT(model.set_branch_color(n2, BranchColor::Green));
    EXPECT(model.effective_branch_color(n3) == BranchColor::Green);
    EXPECT(model.effective_branch_color(n4) == BranchColor::None);

    auto rows = model.visible_rows(nullptr);
    EXPECT(rows.size() == 4);
    EXPECT(rows[0].color == BranchColor::Blue && rows[1].color == BranchColor::Green && rows[2].color == BranchColor::Green && rows[3].color == BranchColor::None);

    // Tags survive the TreeTabs.db round trip, and stored values decode defensively.
    auto snapshot = model.snapshot_persisted_state();
    HashMap<SessionTabId, PersistedTreeNodeState> saved;
    for (auto const& state : snapshot)
        saved.set(state.session_tab_id, state);
    EXPECT(saved.get(201)->color == BranchColor::Blue);
    EXPECT(saved.get(202)->color == BranchColor::Green);
    EXPECT(saved.get(204)->color == BranchColor::None);

    TreeTabModel restored;
    auto r1 = restored.add_tab(&t1, 201);
    auto r2 = restored.add_tab(&t2, 202);
    auto r3 = restored.add_tab(&t3, 203);
    restored.add_tab(&t4, 204);
    restored.apply_persisted_state(saved);
    EXPECT(restored.branch_color(r1) == BranchColor::Blue);
    EXPECT(restored.branch_color(r2) == BranchColor::Green);
    EXPECT(restored.effective_branch_color(r3) == BranchColor::Green);

    EXPECT(branch_color_from_storage(0) == BranchColor::None);
    EXPECT(branch_color_from_storage(static_cast<i64>(BranchColor::Pink)) == BranchColor::Pink);
    EXPECT(branch_color_from_storage(BRANCH_COLOR_COUNT) == BranchColor::None);
    EXPECT(branch_color_from_storage(-3) == BranchColor::None);

    // Replacing a tab (discarding it) keeps its tag.
    Tab t2b(22);
    model.replace_tab(&t2, &t2b);
    EXPECT(model.branch_color(*model.node_id_for_tab(&t2b)) == BranchColor::Green);
}

static void test_filtered_rows_show_matches_with_context()
{
    Tab t1(1), t2(2), t3(3), t4(4), t5(5);
    TreeTabModel model;
    auto n1 = model.add_tab(&t1, 301);
    auto n2 = model.add_tab(&t2, 302, n1);
    model.add_tab(&t3, 303, n2);
    model.add_tab(&t4, 304, n1);
    model.add_tab(&t5, 305);
    model.set_collapsed(n1, true);

    // Collapsed branches open up to show a deep match, with its ancestors.
    auto rows = model.filtered_rows([](Tab const& tab) { return tab.id == 3; });
    auto ids = row_ids_of(rows);
    EXPECT(ids.size() == 3 && ids[0] == 1 && ids[1] == 2 && ids[2] == 3);
    EXPECT(rows[0].depth == 0 && rows[1].depth == 1 && rows[2].depth == 2);
    EXPECT(!rows[0].collapsed && rows[0].has_children && !rows[2].has_children);
    EXPECT(rows[2].is_last_sibling);

    rows = model.filtered_rows([](Tab const& tab) { return tab.id == 4 || tab.id == 5; });
    ids = row_ids_of(rows);
    EXPECT(ids.size() == 3 && ids[0] == 1 && ids[1] == 4 && ids[2] == 5);
    // Sibling 2 is filtered out, so 4 is the last shown child.
    EXPECT(rows[1].is_last_sibling);

    EXPECT(model.filtered_rows([](Tab const&) { return false; }).is_empty());
    EXPECT(model.filtered_rows([](Tab const&) { return true; }).size() == 5);
    // The filter doesn't change the stored collapsed state.
    EXPECT(model.visible_rows(nullptr).size() == 2);
}

static void test_filter_text_matching()
{
    using Superbird::TreeTabs::tab_matches_filter;
    EXPECT(tab_matches_filter("Ladybird Browser", "https://ladybird.org/", ""));
    EXPECT(tab_matches_filter("Ladybird Browser", "https://ladybird.org/", "   "));
    EXPECT(tab_matches_filter("Ladybird Browser", "https://ladybird.org/", "BROWSER"));
    EXPECT(tab_matches_filter("Ladybird Browser", "https://ladybird.org/", "org"));
    EXPECT(tab_matches_filter("Ladybird Browser", "https://ladybird.org/", "browser org"));
    EXPECT(!tab_matches_filter("Ladybird Browser", "https://ladybird.org/", "browser github"));
    EXPECT(!tab_matches_filter("", "", "x"));
    EXPECT(tab_matches_filter("Caf\xC3\xA9", "", "caf\xC3\xA9"));
}

static void test_auto_branch_colors()
{
    // Pattern cycles deterministically through the 8 branch colors.
    EXPECT(auto_branch_color_for_index(0) == BranchColor::Red);
    EXPECT(auto_branch_color_for_index(1) == BranchColor::Orange);
    EXPECT(auto_branch_color_for_index(2) == BranchColor::Yellow);
    EXPECT(auto_branch_color_for_index(3) == BranchColor::Green);
    EXPECT(auto_branch_color_for_index(4) == BranchColor::Teal);
    EXPECT(auto_branch_color_for_index(5) == BranchColor::Blue);
    EXPECT(auto_branch_color_for_index(6) == BranchColor::Purple);
    EXPECT(auto_branch_color_for_index(7) == BranchColor::Pink);
    EXPECT(auto_branch_color_for_index(8) == BranchColor::Red);
    EXPECT(auto_branch_color_for_index(9) == BranchColor::Orange);

    // Off by default.
    Tab r1(1), c1a(2), c1b(3), g1(4), solo(5), r2(6), c2a(7), r3(8), c3a(9);
    TreeTabModel model;
    EXPECT(!model.auto_branch_colors_enabled());

    auto n_r1 = model.add_tab(&r1, 401);
    auto n_c1a = model.add_tab(&c1a, 402, n_r1);
    auto n_c1b = model.add_tab(&c1b, 403, n_r1);
    auto n_g1 = model.add_tab(&g1, 404, n_c1b);
    auto n_solo = model.add_tab(&solo, 405);
    auto n_r2 = model.add_tab(&r2, 406);
    auto n_c2a = model.add_tab(&c2a, 407, n_r2);
    auto n_r3 = model.add_tab(&r3, 408);
    auto n_c3a = model.add_tab(&c3a, 409, n_r3);

    EXPECT(model.effective_branch_color(n_r1) == BranchColor::None);
    EXPECT(model.effective_branch_color(n_g1) == BranchColor::None);

    // Turning auto branch colors on colors every root branch in the predictable sequence;
    // solo roots stay uncolored and do not consume a slot.
    model.set_auto_branch_colors(true);
    EXPECT(model.auto_branch_colors_enabled());
    EXPECT(model.effective_branch_color(n_r1) == BranchColor::Red);
    EXPECT(model.effective_branch_color(n_c1a) == BranchColor::Red);
    EXPECT(model.effective_branch_color(n_c1b) == BranchColor::Red);
    EXPECT(model.effective_branch_color(n_g1) == BranchColor::Red);
    EXPECT(model.effective_branch_color(n_solo) == BranchColor::None);
    EXPECT(model.effective_branch_color(n_r2) == BranchColor::Orange);
    EXPECT(model.effective_branch_color(n_c2a) == BranchColor::Orange);
    EXPECT(model.effective_branch_color(n_r3) == BranchColor::Yellow);
    EXPECT(model.effective_branch_color(n_c3a) == BranchColor::Yellow);

    auto rows = model.visible_rows(nullptr);
    EXPECT(rows.size() == 9);
    EXPECT(rows[0].color == BranchColor::Red && rows[3].color == BranchColor::Red);
    EXPECT(rows[4].color == BranchColor::None); // solo root
    EXPECT(rows[5].color == BranchColor::Orange && rows[6].color == BranchColor::Orange);
    EXPECT(rows[7].color == BranchColor::Yellow && rows[8].color == BranchColor::Yellow);

    // Manual override on a branch root wins and does not shift sibling branch slots.
    EXPECT(model.set_branch_color(n_r2, BranchColor::Purple));
    EXPECT(model.effective_branch_color(n_r2) == BranchColor::Purple);
    EXPECT(model.effective_branch_color(n_c2a) == BranchColor::Purple);
    EXPECT(model.effective_branch_color(n_r3) == BranchColor::Yellow);

    // Manual override on a sub-branch overrides only that subtree.
    EXPECT(model.set_branch_color(n_c1b, BranchColor::Blue));
    EXPECT(model.effective_branch_color(n_r1) == BranchColor::Red);
    EXPECT(model.effective_branch_color(n_c1a) == BranchColor::Red);
    EXPECT(model.effective_branch_color(n_c1b) == BranchColor::Blue);
    EXPECT(model.effective_branch_color(n_g1) == BranchColor::Blue);

    // Manual tag on a solo root works without consuming a branch slot.
    EXPECT(model.set_branch_color(n_solo, BranchColor::Teal));
    EXPECT(model.effective_branch_color(n_solo) == BranchColor::Teal);
    EXPECT(model.effective_branch_color(n_r3) == BranchColor::Yellow);

    // Persistence only stores explicit manual overrides, not auto-derived colors.
    auto snapshot = model.snapshot_persisted_state();
    HashMap<SessionTabId, BranchColor> persisted_colors;
    for (auto const& state : snapshot)
        persisted_colors.set(state.session_tab_id, state.color);
    EXPECT(persisted_colors.get(401).value() == BranchColor::None);
    EXPECT(persisted_colors.get(403).value() == BranchColor::Blue);
    EXPECT(persisted_colors.get(405).value() == BranchColor::Teal);
    EXPECT(persisted_colors.get(406).value() == BranchColor::Purple);
    EXPECT(persisted_colors.get(408).value() == BranchColor::None);

    // Quick filter keeps each matched branch's true color even when earlier branches are filtered out.
    auto filtered = model.filtered_rows([](Tab const& tab) { return tab.id == 9; });
    EXPECT(filtered.size() == 2);
    EXPECT(filtered[0].tab->id == 8 && filtered[0].color == BranchColor::Yellow);
    EXPECT(filtered[1].tab->id == 9 && filtered[1].color == BranchColor::Yellow);

    // Hidden tabs (another workspace) do not shift the active workspace's color sequence.
    Function<bool(Tab const&)> hide_first_two_branches = [](Tab const& tab) { return tab.id <= 7; };
    EXPECT(model.effective_branch_color(n_r3, hide_first_two_branches) == BranchColor::Red);
    EXPECT(model.effective_branch_color(n_c3a, hide_first_two_branches) == BranchColor::Red);
    auto ws_rows = model.visible_rows(nullptr, {}, {}, hide_first_two_branches);
    // Visible rows for r3/c3a (at the end of the unfiltered list) carry Red for workspace 2.
    EXPECT(ws_rows[7].tab->id == 8 && ws_rows[7].color == BranchColor::Red);
    EXPECT(ws_rows[8].tab->id == 9 && ws_rows[8].color == BranchColor::Red);
    auto ws_filtered = model.filtered_rows([](Tab const& tab) { return tab.id == 9; }, hide_first_two_branches);
    EXPECT(ws_filtered.size() == 2 && ws_filtered[0].color == BranchColor::Red && ws_filtered[1].color == BranchColor::Red);

    // Clearing a manual override falls back to the automatic color.
    EXPECT(model.set_branch_color(n_r2, BranchColor::None));
    EXPECT(model.effective_branch_color(n_r2) == BranchColor::Orange);

    // Collapsed branches keep their automatic color.
    EXPECT(model.set_collapsed(n_r3, true));
    rows = model.visible_rows(nullptr);
    EXPECT(rows.last().tab->id == 8 && rows.last().collapsed && rows.last().color == BranchColor::Yellow);

    // Closing a branch's last child makes it a solo root again (no auto color).
    model.remove_node(n_c3a, WebView::TreeTabCloseBehavior::AttachToGrandparent);
    EXPECT(model.effective_branch_color(n_r3) == BranchColor::None);

    // Turning auto branch colors off clears automatic colors while keeping manual overrides.
    model.set_auto_branch_colors(false);
    EXPECT(model.effective_branch_color(n_r1) == BranchColor::None);
    EXPECT(model.effective_branch_color(n_c1b) == BranchColor::Blue);
    EXPECT(model.effective_branch_color(n_g1) == BranchColor::Blue);
    EXPECT(model.effective_branch_color(n_solo) == BranchColor::Teal);
    EXPECT(model.effective_branch_color(n_r2) == BranchColor::None);

    // Wraparound across more than 8 branches.
    TreeTabModel many;
    many.set_auto_branch_colors(true);
    Vector<Tab> roots;
    Vector<Tab> children;
    roots.ensure_capacity(10);
    children.ensure_capacity(10);
    for (int i = 0; i < 10; ++i) {
        roots.append(Tab(100 + i));
        children.append(Tab(200 + i));
    }
    Vector<TreeNodeId> root_ids;
    for (size_t i = 0; i < 10; ++i) {
        auto rid = many.add_tab(&roots[i], static_cast<SessionTabId>(100 + i));
        many.add_tab(&children[i], static_cast<SessionTabId>(200 + i), rid);
        root_ids.append(rid);
    }
    for (size_t i = 0; i < 10; ++i)
        EXPECT(many.effective_branch_color(root_ids[i]) == auto_branch_color_for_index(i));
    EXPECT(many.effective_branch_color(root_ids[8]) == BranchColor::Red);
    EXPECT(many.effective_branch_color(root_ids[9]) == BranchColor::Orange);
}

int main()
{
    test_opener_nesting_and_linearization();
    test_collapse_rollup_and_reveal();
    test_drag_drop_indent_outdent_and_cycle_guard();
    test_close_orphan_policies();
    test_replace_tab_and_persistence_roundtrip();
    test_multi_selection();
    test_branch_color_tags_and_persistence();
    test_auto_branch_colors();
    test_filtered_rows_show_matches_with_context();
    test_filter_text_matching();
    std::printf("All TreeTabModel unit tests passed!\n");
    return 0;
}
