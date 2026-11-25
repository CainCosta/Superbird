/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashTable.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitLayout.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitViewActions.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace Superbird::Split;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                          \
        }                                                                          \
    } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static double sum(std::vector<double> const& values)
{
    double total = 0;
    for (auto value : values)
        total += value;
    return total;
}

static void test_create_split()
{
    EXPECT(!SplitLayout::create(1, 1).has_value());
    auto layout = SplitLayout::create(1, 2).value();
    EXPECT(layout.pane_count() == 2);
    EXPECT((layout.panes() == std::vector<PaneId> { 1, 2 }));
    EXPECT(layout.focused() == 1);
    EXPECT(layout.root().orientation == Orientation::Horizontal);
    EXPECT(near(layout.ratios_around(1)[0], 0.5));
    auto stacked = SplitLayout::create(3, 4, Orientation::Vertical).value();
    EXPECT(stacked.root().orientation == Orientation::Vertical);
}

static void test_focus_switch()
{
    auto layout = SplitLayout::create(1, 2).value();
    EXPECT(layout.focus(2));
    EXPECT(layout.focused() == 2);
    EXPECT(!layout.focus(99));
    EXPECT(layout.focused() == 2);
    EXPECT(layout.next_pane(1) == 1);
    EXPECT(layout.next_pane(-1) == 1);
    layout.add_pane(3, 2, Orientation::Horizontal);
    EXPECT(layout.focused() == 3);
    EXPECT(layout.next_pane(1) == 1);
    EXPECT(layout.next_pane(-1) == 2);
}

static void test_add_panes_up_to_grid()
{
    auto layout = SplitLayout::create(1, 2).value();
    // Same orientation joins the existing split, splitting the neighbour's share.
    EXPECT(layout.add_pane(3, 2, Orientation::Horizontal));
    EXPECT((layout.panes() == std::vector<PaneId> { 1, 2, 3 }));
    auto ratios = layout.ratios_around(3);
    EXPECT(ratios.size() == 3 && near(sum(ratios), 1) && near(ratios[0], 0.5) && near(ratios[1], 0.25) && near(ratios[2], 0.25));
    // The other orientation nests a split in the pane's place.
    EXPECT(layout.add_pane(4, 1, Orientation::Vertical));
    EXPECT((layout.panes() == std::vector<PaneId> { 1, 4, 2, 3 }));
    EXPECT(layout.ratios_around(4).size() == 2);
    // Four is the limit, and a pane can't appear twice.
    EXPECT(!layout.add_pane(5, 1, Orientation::Horizontal));
    EXPECT(!layout.add_pane(2, 1, Orientation::Horizontal));
    EXPECT(!SplitLayout::create(1, 2).value().add_pane(3, 77, Orientation::Horizontal));
}

static void test_grid()
{
    EXPECT(!SplitLayout::grid({ 1 }).has_value());
    EXPECT(!SplitLayout::grid({ 1, 2, 3, 4, 5 }).has_value());
    EXPECT(!SplitLayout::grid({ 1, 2, 2 }).has_value());
    auto four = SplitLayout::grid({ 1, 2, 3, 4 }).value();
    EXPECT(four.root().orientation == Orientation::Vertical);
    EXPECT(four.root().children.size() == 2);
    EXPECT(four.root().children[0]->orientation == Orientation::Horizontal);
    EXPECT((four.panes() == std::vector<PaneId> { 1, 2, 3, 4 }));
    auto three = SplitLayout::grid({ 1, 2, 3 }).value();
    EXPECT(three.root().children[1]->is_leaf());
    EXPECT(SplitLayout::grid({ 5, 6 }).value().pane_count() == 2);
}

static void test_swap_and_replace()
{
    auto layout = SplitLayout::create(1, 2).value();
    layout.set_ratios_around(1, { 0.7, 0.3 });
    EXPECT(layout.swap(1, 2));
    EXPECT((layout.panes() == std::vector<PaneId> { 2, 1 }));
    // Ratios stay with the places.
    EXPECT(near(layout.ratios_around(2)[0], 0.7));
    EXPECT(!layout.swap(1, 1));
    EXPECT(!layout.swap(1, 9));

    EXPECT(layout.replace_pane(1, 10));
    EXPECT(layout.focused() == 10);
    EXPECT(!layout.replace_pane(10, 2));
    EXPECT((layout.panes() == std::vector<PaneId> { 2, 10 }));
}

static void test_close_pane_collapses()
{
    auto layout = SplitLayout::grid({ 1, 2, 3, 4 }).value();
    layout.focus(2);
    EXPECT(layout.remove_pane(2));
    // The top row of one collapsed into its remaining pane; focus moved to the pane that took its place.
    EXPECT((layout.panes() == std::vector<PaneId> { 1, 3, 4 }));
    EXPECT(layout.root().children[0]->is_leaf());
    EXPECT(layout.focused() == 1);

    EXPECT(layout.remove_pane(4));
    EXPECT((layout.panes() == std::vector<PaneId> { 1, 3 }));
    EXPECT(layout.root().children.size() == 2);
    EXPECT(near(sum(layout.root().ratios), 1));

    // Down to one pane: the root collapses to a leaf, which the host treats as "unsplit".
    EXPECT(layout.remove_pane(3));
    EXPECT(layout.pane_count() == 1);
    EXPECT(layout.root().is_leaf());
    EXPECT(layout.focused() == 1);
    EXPECT(!layout.remove_pane(1));
    EXPECT(!layout.remove_pane(42));
}

static void test_remove_from_three_way_split_redistributes()
{
    auto layout = SplitLayout::create(1, 2).value();
    layout.add_pane(3, 2, Orientation::Horizontal);
    layout.set_ratios_around(1, { 0.5, 0.3, 0.2 });
    layout.focus(3);
    EXPECT(layout.remove_pane(2));
    auto ratios = layout.ratios_around(1);
    EXPECT(ratios.size() == 2 && near(sum(ratios), 1));
    EXPECT(near(ratios[0], 0.5 / 0.7) && near(ratios[1], 0.2 / 0.7));
    EXPECT(layout.focused() == 3);
}

static void test_ratios()
{
    auto normalized = normalize_ratios({ 3, 1 });
    EXPECT(near(normalized[0], 0.75) && near(normalized[1], 0.25));
    normalized = normalize_ratios({ 1, 0 });
    EXPECT(near(normalized[1], MINIMUM_RATIO) && near(normalized[0], 1 - MINIMUM_RATIO));
    normalized = normalize_ratios({ 0, 0, 0 });
    EXPECT(near(normalized[0], 1.0 / 3));
    normalized = normalize_ratios({ -5, std::numeric_limits<double>::quiet_NaN(), 2 });
    EXPECT(near(sum(normalized), 1) && normalized[0] >= MINIMUM_RATIO - 1e-12 && normalized[1] >= MINIMUM_RATIO - 1e-12);
    normalized = normalize_ratios({ 0.98, 0.01, 0.01 });
    EXPECT(near(sum(normalized), 1));
    for (auto value : normalized)
        EXPECT(value >= MINIMUM_RATIO - 1e-9);
    EXPECT(normalize_ratios({}).empty());

    auto layout = SplitLayout::create(1, 2).value();
    EXPECT(!layout.set_ratios_around(1, { 1, 1, 1 }));
    EXPECT(layout.set_ratios_around(2, { 2, 6 }));
    EXPECT(near(layout.ratios_around(1)[0], 0.25));
    layout.equalize();
    EXPECT(near(layout.ratios_around(1)[0], 0.5));
    EXPECT(layout.ratios_around(77) == std::vector<double> { 1.0 });
}

static void test_toggle_orientation()
{
    auto layout = SplitLayout::create(1, 2).value();
    layout.toggle_orientation();
    EXPECT(layout.root().orientation == Orientation::Vertical);
    layout.toggle_orientation();
    EXPECT(layout.root().orientation == Orientation::Horizontal);
}

class FakeSplitHandler final : public Superbird::SplitCommandHandler {
public:
    virtual bool can_run(Superbird::SplitCommand command) const override
    {
        // Only splitting is possible until a split exists.
        bool splits = command == Superbird::SplitCommand::SideBySide || command == Superbird::SplitCommand::Stacked || command == Superbird::SplitCommand::Grid;
        return split_active ? !splits : splits;
    }
    virtual void run(Superbird::SplitCommand command) override { ran.push_back(command); }
    bool split_active { false };
    std::vector<Superbird::SplitCommand> ran;
};

static void test_split_actions()
{
    using namespace Superbird;
    HashTable<StringView> ids;
    for (auto const& definition : SPLIT_VIEW_ACTIONS) {
        EXPECT(definition.id.length() > 11 && definition.id.substring_view(0, 11) == "split_view."sv);
        EXPECT(ids.set(definition.id) == HashSetResult::InsertedNewEntry);
    }
    ModuleRegistry registry;
    EXPECT(registry.register_module({ .id = SPLIT_VIEW_MODULE_ID, .name = "Split View"sv, .description = ""sv, .enabled_by_default = true }));
    FakeSplitHandler handler;
    register_split_view_actions(registry, handler);
    EXPECT(registry.matching_actions().size() == 3);
    handler.split_active = true;
    EXPECT(registry.matching_actions().size() == SPLIT_VIEW_ACTIONS.size() - 3);
    auto unsplit = registry.matching_actions("unsplit"sv);
    EXPECT(unsplit.size() == 1);
    unsplit[0]->trigger();
    EXPECT(handler.ran.size() == 1 && handler.ran[0] == SplitCommand::Unsplit);
    registry.set_enabled(SPLIT_VIEW_MODULE_ID, false);
    EXPECT(registry.matching_actions().is_empty());
}

static std::optional<int64_t> save_as_hundreds(PaneId pane)
{
    return pane * 100;
}

static void test_serialize_round_trip()
{
    // Three panes: 1 | (2 over 3), with moved ratios and the focus on 3.
    auto layout = SplitLayout::create(1, 2);
    EXPECT(layout.has_value());
    EXPECT(layout->add_pane(3, 2, Orientation::Vertical));
    EXPECT(layout->set_ratios_around(1, { 0.3, 0.7 }));
    EXPECT(layout->set_ratios_around(3, { 0.25, 0.75 }));
    EXPECT(layout->focus(3));

    auto text = layout->serialize(save_as_hundreds);
    EXPECT(text.has_value());
    EXPECT(*text == "300|h(3000:100,7000:v(2500:200,7500:300))");

    // Saved ids map to new panes (as tabs get new pointers and pane ids after a restart).
    auto restored = SplitLayout::deserialize(*text, [](int64_t saved) -> std::optional<PaneId> { return saved / 100 + 10; });
    EXPECT(restored.has_value());
    EXPECT(restored->panes() == std::vector<PaneId>({ 11, 12, 13 }));
    EXPECT(restored->focused() == 13);
    EXPECT(restored->root().orientation == Orientation::Horizontal);
    EXPECT(std::fabs(restored->ratios_around(11)[0] - 0.3) < 1e-6);
    EXPECT(std::fabs(restored->ratios_around(13)[1] - 0.75) < 1e-6);
    EXPECT(restored->serialize([](PaneId pane) -> std::optional<int64_t> { return (pane - 10) * 100; }) == text);

    // A pane with no saved id can't be saved.
    EXPECT(!layout->serialize([](PaneId pane) -> std::optional<int64_t> {
        if (pane == 2)
            return {};
        return pane;
    }));
}

static void test_deserialize_with_overlapping_ids()
{
    // Saved ids 1 and 2 map to panes 2 and 1: the swap mustn't trip over a pane that's still to be renamed.
    auto restored = SplitLayout::deserialize("1|h(5000:1,5000:2)", [](int64_t saved) -> std::optional<PaneId> { return 3 - saved; });
    EXPECT(restored.has_value());
    EXPECT(restored->panes() == std::vector<PaneId>({ 2, 1 }));
    EXPECT(restored->focused() == 2);
}

static void test_deserialize_drops_missing_tabs()
{
    auto text = "2|h(3333:1,3333:2,3334:3)";
    // Tab 2 (the focused one) didn't come back: the others share its room and the focus moves on.
    auto restored = SplitLayout::deserialize(text, [](int64_t saved) -> std::optional<PaneId> {
        if (saved == 2)
            return {};
        return saved;
    });
    EXPECT(restored.has_value());
    EXPECT(restored->panes() == std::vector<PaneId>({ 1, 3 }));
    EXPECT(restored->contains(restored->focused()));

    // With only one tab back, there's no split.
    EXPECT(!SplitLayout::deserialize("1|h(5000:1,5000:2)", [](int64_t saved) -> std::optional<PaneId> {
        if (saved == 1)
            return 7;
        return {};
    }));
    // Two saved ids finding the same pane count as one.
    EXPECT(!SplitLayout::deserialize("1|h(5000:1,5000:2)", [](int64_t) -> std::optional<PaneId> { return 5; }));
}

static void test_deserialize_rejects_bad_text()
{
    auto identity = [](int64_t saved) -> std::optional<PaneId> { return saved; };
    char const* bad[] = {
        "",
        "1",
        "1|",
        "1|5",
        "x|h(5000:1,5000:2)",
        "1|h(5000:1)",
        "1|h(5000:1,5000:2",
        "1|h(5000:1,5000:2))",
        "1|h(5000:1,5000:1)",
        "1|q(5000:1,5000:2)",
        "1|h(1:1,1:2,1:3,1:4,1:5)",
        "1|h(-5:1,5000:2)",
        "1|h(5000:1,5000:v(5000:v(5000:v(5000:v(5000:v(5000:2,5000:3),5000:4),5000:5),5000:6),5000:7))",
    };
    for (auto const* text : bad)
        EXPECT(!SplitLayout::deserialize(text, identity));

    // Ratios are normalized as they're read.
    auto restored = SplitLayout::deserialize("9|v(1:1,3:2)", identity);
    EXPECT(restored.has_value());
    EXPECT(restored->focused() == 1);
    EXPECT(std::fabs(restored->ratios_around(1)[0] - 0.25) < 1e-6);
}

int main()
{
    test_serialize_round_trip();
    test_deserialize_with_overlapping_ids();
    test_deserialize_drops_missing_tabs();
    test_deserialize_rejects_bad_text();
    test_split_actions();
    test_create_split();
    test_focus_switch();
    test_add_panes_up_to_grid();
    test_grid();
    test_swap_and_replace();
    test_close_pane_collapses();
    test_remove_from_three_way_split_redistributes();
    test_ratios();
    test_toggle_orientation();
    std::printf("All SplitLayout unit tests passed!\n");
    return 0;
}
