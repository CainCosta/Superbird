/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashTable.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceModel.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacePresets.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacesActions.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace Superbird::Workspaces;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                          \
        }                                                                          \
    } while (0)

static void test_starts_with_default_workspace()
{
    WorkspaceModel model;
    EXPECT(model.workspaces().size() == 1);
    EXPECT(model.workspaces()[0].name == DEFAULT_WORKSPACE_NAME);
    EXPECT(model.active_workspace() == model.workspaces()[0].id);
}

static void test_crud()
{
    WorkspaceModel model;
    auto work = model.create_workspace("  Work  ", "\xF0\x9F\x92\xBC", 0x12345678);
    auto unnamed = model.create_workspace("");
    EXPECT(model.workspaces().size() == 3);
    EXPECT(model.workspace(work)->name == "Work");
    EXPECT(model.workspace(work)->icon == "\xF0\x9F\x92\xBC");
    EXPECT(model.workspace(work)->tint == 0x345678u);
    EXPECT(model.workspace(unnamed)->name == "Workspace 3");
    EXPECT(work != unnamed);

    EXPECT(model.rename_workspace(work, "Office\tStuff"));
    EXPECT(model.workspace(work)->name == "Office Stuff");
    EXPECT(!model.rename_workspace(work, "   "));
    EXPECT(!model.rename_workspace(999, "x"));
    std::string long_name(100, 'a');
    long_name += "\xC3\xA9";
    EXPECT(model.rename_workspace(work, long_name));
    EXPECT(model.workspace(work)->name.size() == MAXIMUM_WORKSPACE_NAME_LENGTH);

    // Truncation never splits a UTF-8 character.
    std::string accented;
    for (int i = 0; i < 40; ++i)
        accented += "\xC3\xA9";
    EXPECT(model.rename_workspace(unnamed, accented));
    auto const& truncated = model.workspace(unnamed)->name;
    EXPECT(truncated.size() == MAXIMUM_WORKSPACE_NAME_LENGTH && (static_cast<unsigned char>(truncated.back()) == 0xA9));

    EXPECT(model.set_icon(work, "x"));
    EXPECT(model.set_tint(work, {}));
    EXPECT(!model.workspace(work)->tint.has_value());

    EXPECT(model.move_workspace(unnamed, 0));
    EXPECT(model.workspaces()[0].id == unnamed);
    EXPECT(!model.move_workspace(unnamed, 0));
    EXPECT(model.move_workspace(unnamed, 99));
    EXPECT(model.workspaces().back().id == unnamed);
}

static void test_tab_assignment_and_visibility()
{
    WorkspaceModel model;
    auto home = model.active_workspace();
    auto work = model.create_workspace("Work");

    model.add_tab(1);
    model.add_tab(2, work);
    model.add_tab(3, 12345); // Unknown workspace: joins the active one.
    EXPECT(model.workspace_of(1) == home);
    EXPECT(model.workspace_of(2) == work);
    EXPECT(model.workspace_of(3) == home);
    EXPECT(model.tab_count(home) == 2);
    EXPECT(model.tab_count(work) == 1);

    EXPECT(model.is_visible(1, false));
    EXPECT(!model.is_visible(2, false));
    // Pinned tabs show everywhere by default...
    EXPECT(model.is_visible(2, true));
    model.set_pinned_tabs_are_global(false);
    EXPECT(!model.is_visible(2, true));
    // ...and tabs the model doesn't know about yet show rather than vanish.
    EXPECT(model.is_visible(99, false));

    // Re-adding without a workspace keeps the assignment.
    model.add_tab(2);
    EXPECT(model.workspace_of(2) == work);

    EXPECT(model.assign_tab(1, work));
    EXPECT(!model.assign_tab(1, work));
    EXPECT(!model.assign_tab(1, 777));
    EXPECT(model.workspace_of(1) == work);

    std::vector<TabKey> strip { 3, 2, 1 };
    auto in_work = model.tabs_in(work, strip);
    EXPECT(in_work.size() == 2 && in_work[0] == 2 && in_work[1] == 1);

    model.remove_tab(2);
    EXPECT(!model.workspace_of(2).has_value());
    EXPECT(model.tab_count(work) == 1);
}

static void test_switch_restores_last_active_tab_with_fallbacks()
{
    WorkspaceModel model;
    auto home = model.active_workspace();
    auto work = model.create_workspace("Work");
    auto empty = model.create_workspace("Empty");
    model.add_tab(1);
    model.add_tab(2);
    model.add_tab(10, work);
    model.add_tab(11, work);
    std::vector<TabKey> strip { 1, 2, 10, 11 };

    model.tab_activated(2);
    // First visit to Work: its first tab in strip order.
    auto result = model.activate(work, strip);
    EXPECT(result.has_value());
    EXPECT(result->previous == home && result->current == work);
    EXPECT(result->tab_to_activate == 10);
    EXPECT(model.active_workspace() == work);
    EXPECT(model.is_visible(10, false) && !model.is_visible(1, false));

    model.tab_activated(11);
    // Back home: the tab that was active there.
    result = model.activate(home, strip);
    EXPECT(result->tab_to_activate == 2);
    // And back to Work: the tab last active there.
    result = model.activate(work, strip);
    EXPECT(result->tab_to_activate == 11);

    // The remembered tab closed: fall back to the first one.
    model.remove_tab(11);
    std::vector<TabKey> strip_without_11 { 1, 2, 10 };
    model.activate(home, strip_without_11);
    result = model.activate(work, strip_without_11);
    EXPECT(result->tab_to_activate == 10);

    // An empty workspace asks for a new tab.
    result = model.activate(empty, strip_without_11);
    EXPECT(result.has_value() && !result->tab_to_activate.has_value());

    // No-ops.
    EXPECT(!model.activate(empty, strip).has_value());
    EXPECT(!model.activate(4242, strip).has_value());
}

static void test_activating_tab_elsewhere_asks_to_follow()
{
    WorkspaceModel model;
    auto work = model.create_workspace("Work");
    model.add_tab(1);
    model.add_tab(2, work);
    EXPECT(!model.tab_activated(1).has_value());
    EXPECT(model.tab_activated(2) == work);
    EXPECT(!model.tab_activated(404).has_value());
}

static void test_next_workspace_wraps()
{
    WorkspaceModel model;
    EXPECT(!model.next_workspace(1).has_value());
    auto first = model.active_workspace();
    auto second = model.create_workspace("Two");
    auto third = model.create_workspace("Three");
    EXPECT(model.next_workspace(1) == second);
    EXPECT(model.next_workspace(-1) == third);
    std::vector<TabKey> strip;
    model.activate(third, strip);
    EXPECT(model.next_workspace(1) == first);
}

static void test_remove_workspace_moves_tabs_and_active()
{
    WorkspaceModel model;
    auto home = model.active_workspace();
    auto work = model.create_workspace("Work");
    auto play = model.create_workspace("Play");
    model.add_tab(1);
    model.add_tab(2, work);
    model.add_tab(3, play);
    std::vector<TabKey> strip { 1, 2, 3 };
    model.activate(work, strip);

    EXPECT(!model.remove_workspace(work, work));
    EXPECT(!model.remove_workspace(work, 999));
    EXPECT(model.remove_workspace(work));
    // Tabs and the active state went to the workspace before it.
    EXPECT(model.workspace_of(2) == home);
    EXPECT(model.active_workspace() == home);
    EXPECT(model.workspaces().size() == 2);

    EXPECT(model.remove_workspace(home, play));
    EXPECT(model.workspace_of(1) == play && model.workspace_of(2) == play);
    // The last workspace stays.
    EXPECT(!model.remove_workspace(play));
    EXPECT(model.workspaces().size() == 1);
}

static void test_snapshot_and_rows_round_trip()
{
    WorkspaceModel model;
    auto work = model.create_workspace("Work", "W", 0xFF8800);
    auto play = model.create_workspace("Play");
    model.move_workspace(play, 0);
    model.add_tab(100);
    model.add_tab(200, work);
    model.add_tab(300, play);
    std::vector<TabKey> strip { 100, 200, 300 };
    model.activate(work, strip);

    auto snapshot = model.snapshot();
    EXPECT(snapshot.active == work);
    EXPECT(snapshot.workspaces.size() == 3 && snapshot.workspaces[0].id == play);
    EXPECT(snapshot.assignments.size() == 3);

    auto rows = snapshot_to_rows(7, snapshot);
    EXPECT(rows.workspaces.size() == 3);
    EXPECT(rows.workspaces[1].position == 1);
    EXPECT(rows.workspaces[2].is_active == (rows.workspaces[2].id == work));
    // Another window's rows mixed in are ignored.
    auto other = snapshot_to_rows(8, WorkspaceModel().snapshot());
    rows.workspaces.insert(rows.workspaces.begin(), other.workspaces.begin(), other.workspaces.end());

    auto back = rows_to_snapshot(7, rows);
    EXPECT(back == snapshot);

    auto restored = WorkspaceModel::from_snapshot(back);
    EXPECT(restored.snapshot() == snapshot);
    // New ids don't collide with restored ones.
    auto fresh = restored.create_workspace("Fresh");
    EXPECT(fresh > work && fresh > play);
}

static void test_snapshot_validation()
{
    WorkspaceModel::Snapshot broken;
    broken.workspaces = { { .id = 5, .name = "  ", .icon = {}, .tint = {} }, { .id = 5, .name = "Dup", .icon = {}, .tint = {} }, { .id = -1, .name = "Bad", .icon = {}, .tint = {} } };
    broken.active = 42;
    broken.assignments = { { 1, 5 }, { 2, 99 } };
    auto model = WorkspaceModel::from_snapshot(broken);
    EXPECT(model.workspaces().size() == 1);
    EXPECT(model.workspaces()[0].id == 5);
    EXPECT(model.workspaces()[0].name == "Workspace 1");
    EXPECT(model.active_workspace() == 5);
    EXPECT(model.workspace_of(1) == 5);
    EXPECT(!model.workspace_of(2).has_value());

    auto empty = WorkspaceModel::from_snapshot({});
    EXPECT(empty.workspaces().size() == 1 && empty.workspaces()[0].name == DEFAULT_WORKSPACE_NAME);
}

class FakeHandler final : public Superbird::WorkspacesCommandHandler {
public:
    virtual bool can_run(Superbird::WorkspacesCommand command, size_t slot) const override
    {
        if (command == Superbird::WorkspacesCommand::SwitchToWorkspace)
            return slot < workspace_count;
        return true;
    }
    virtual void run(Superbird::WorkspacesCommand command, size_t slot) override { ran.push_back({ command, slot }); }
    size_t workspace_count { 0 };
    std::vector<std::pair<Superbird::WorkspacesCommand, size_t>> ran;
};

static void test_actions_and_switch_titles()
{
    using namespace Superbird;
    HashTable<StringView> ids;
    for (auto const& definition : WORKSPACES_ACTIONS) {
        EXPECT(definition.id.length() > 11 && definition.id.substring_view(0, 11) == "workspaces."sv);
        EXPECT(ids.set(definition.id) == HashSetResult::InsertedNewEntry);
    }

    ModuleRegistry registry;
    EXPECT(registry.register_module({ .id = WORKSPACES_MODULE_ID, .name = "Workspaces"sv, .description = ""sv, .enabled_by_default = true }));
    FakeHandler handler;
    register_workspaces_actions(registry, handler);
    // Switch slots are registered but not offered until there are workspaces.
    EXPECT(registry.matching_actions().size() == WORKSPACES_ACTIONS.size());

    std::vector<std::string> names { "Work", "Research ð¬" };
    handler.workspace_count = names.size();
    set_workspace_switch_titles(registry, handler, names);
    EXPECT(registry.matching_actions().size() == WORKSPACES_ACTIONS.size() + 2);
    auto found = registry.matching_actions("research"sv);
    EXPECT(found.size() == 1);
    EXPECT(found[0]->title == "Switch to Workspace: Research ð¬"sv);
    found[0]->trigger();
    EXPECT(handler.ran.size() == 1 && handler.ran[0].first == WorkspacesCommand::SwitchToWorkspace && handler.ran[0].second == 1);

    // Renaming re-titles in place; a very long name is cut on a character boundary.
    names[0] = std::string(200, 'x') + "Ã©";
    set_workspace_switch_titles(registry, handler, names);
    auto long_title = registry.matching_actions(workspace_switch_action_id(0)).at(0)->title;
    EXPECT(long_title.length() <= 128);
    EXPECT(registry.matching_actions("work"sv).size() >= 1);

    registry.set_enabled(WORKSPACES_MODULE_ID, false);
    EXPECT(registry.matching_actions().is_empty());
}

static void test_glyph_icons_round_trip()
{
    for (auto const& info : GLYPHS) {
        auto icon = icon_for_glyph(info.glyph);
        EXPECT(icon.starts_with(GLYPH_ICON_PREFIX));
        EXPECT(glyph_from_icon(icon) == info.glyph);
        EXPECT(glyph_info(info.glyph).key == info.key);
    }
    // Keys are unique.
    for (size_t i = 0; i < GLYPHS.size(); ++i) {
        for (size_t j = i + 1; j < GLYPHS.size(); ++j)
            EXPECT(GLYPHS[i].key != GLYPHS[j].key);
    }
    // Icon text that isn't a glyph (a letter, an emoji, a bare key, an unknown key) shows as text.
    EXPECT(!glyph_from_icon("").has_value());
    EXPECT(!glyph_from_icon("W").has_value());
    EXPECT(!glyph_from_icon("\xF0\x9F\x92\xBC").has_value());
    EXPECT(!glyph_from_icon("home").has_value());
    EXPECT(!glyph_from_icon("glyph:").has_value());
    EXPECT(!glyph_from_icon("glyph:spaceship").has_value());
    EXPECT(!glyph_from_icon("glyph:home ").has_value());
}

static void test_presets()
{
    std::string_view const expected[] = { "Home", "Work", "School", "Focus", "Relax" };
    EXPECT(PRESETS.size() == std::size(expected));
    for (size_t i = 0; i < PRESETS.size(); ++i) {
        EXPECT(PRESETS[i].name == expected[i]);
        // A preset's tint is one the editor offers, so it shows as picked there.
        auto offered = false;
        for (auto const& tint : TINTS)
            offered |= tint.rgb == PRESETS[i].tint;
        EXPECT(offered);
    }

    std::vector<std::string> names { "Default", "  work ", "Studies" };
    EXPECT(!preset_is_taken(PRESETS[0], names));
    EXPECT(preset_is_taken(PRESETS[1], names));
    EXPECT(!preset_is_taken(PRESETS[2], names));
    EXPECT(!preset_is_taken(PRESETS[1], {}));

    // A preset makes a workspace the model keeps as is.
    WorkspaceModel model;
    auto id = model.create_workspace(PRESETS[3].name, icon_for_glyph(PRESETS[3].glyph), PRESETS[3].tint);
    EXPECT(model.workspace(id)->name == "Focus");
    EXPECT(glyph_from_icon(model.workspace(id)->icon) == Glyph::Focus);
    EXPECT(model.workspace(id)->tint == PRESETS[3].tint);

    EXPECT(default_tint_for_new_workspace(0) == TINTS[0].rgb);
    EXPECT(default_tint_for_new_workspace(TINTS.size() + 2) == TINTS[2].rgb);
}

int main()
{
    test_glyph_icons_round_trip();
    test_presets();
    test_actions_and_switch_titles();
    test_starts_with_default_workspace();
    test_crud();
    test_tab_assignment_and_visibility();
    test_switch_restores_last_active_tab_with_fallbacks();
    test_activating_tab_elsewhere_asks_to_follow();
    test_next_workspace_wraps();
    test_remove_workspace_moves_tabs_and_active();
    test_snapshot_and_rows_round_trip();
    test_snapshot_validation();
    std::printf("All Workspaces unit tests passed!\n");
    return 0;
}
