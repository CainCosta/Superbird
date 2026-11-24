/*
 * Copyright (c) 2026, Sidelady contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <cstdio>
#include <AK/Vector.h>
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

int main()
{
    test_opener_nesting_and_linearization();
    test_collapse_rollup_and_reveal();
    test_drag_drop_indent_outdent_and_cycle_guard();
    test_close_orphan_policies();
    test_replace_tab_and_persistence_roundtrip();
    test_multi_selection();
    std::printf("All TreeTabModel unit tests passed!\n");
    return 0;
}
