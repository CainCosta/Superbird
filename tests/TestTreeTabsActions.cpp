/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashTable.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabsActions.h>

#include <cstdio>
#include <cstdlib>

using namespace Superbird;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                          \
        }                                                                          \
    } while (0)

class FakeHandler final : public TreeTabsCommandHandler {
public:
    virtual bool can_run(TreeTabsCommand command) const override { return !unavailable.contains(static_cast<u8>(command)); }
    virtual void run(TreeTabsCommand command) override { ran.append(command); }

    HashTable<u8> unavailable;
    Vector<TreeTabsCommand> ran;
};

static void register_tree_tabs_module(ModuleRegistry& registry)
{
    EXPECT(registry.register_module({
        .id = TREE_TABS_MODULE_ID,
        .name = "Tree-Style Tabs"sv,
        .description = "Nested tabs"sv,
        .enabled_by_default = true,
    }));
}

static ActionDescriptor const* find_action(ModuleRegistry const& registry, StringView id)
{
    for (auto const* action : registry.matching_actions()) {
        if (action->id == id)
            return action;
    }
    return nullptr;
}

static void test_action_table_is_well_formed()
{
    HashTable<StringView> ids;
    HashTable<u8> commands;
    for (auto const& definition : TREE_TABS_ACTIONS) {
        EXPECT(definition.id.length() > 10 && definition.id.substring_view(0, 10) == "tree_tabs."sv);
        EXPECT(!definition.title.is_empty());
        EXPECT(ids.set(definition.id) == HashSetResult::InsertedNewEntry);
        EXPECT(commands.set(static_cast<u8>(definition.command)) == HashSetResult::InsertedNewEntry);
    }
    // Every command has exactly one action, including the branch actions the roadmap requires.
    EXPECT(commands.size() == static_cast<size_t>(TreeTabsCommand::FocusFilter) + 1);
    for (auto id : { "tree_tabs.toggle_branch"sv, "tree_tabs.new_child_tab"sv, "tree_tabs.unload_branch"sv, "tree_tabs.close_branch"sv, "tree_tabs.move_branch_to_new_window"sv })
        EXPECT(ids.contains(id));
}

static void test_all_actions_register_on_the_bus()
{
    ModuleRegistry registry;
    register_tree_tabs_module(registry);
    FakeHandler handler;
    register_tree_tabs_actions(registry, handler);

    auto actions = registry.matching_actions();
    EXPECT(actions.size() == TREE_TABS_ACTIONS.size());
    for (auto const* action : actions) {
        EXPECT(action->module_id == TREE_TABS_MODULE_ID);
        EXPECT(action->category == TREE_TABS_ACTION_CATEGORY);
    }

    // Registering again is idempotent: actions are keyed by id.
    register_tree_tabs_actions(registry, handler);
    EXPECT(registry.matching_actions().size() == TREE_TABS_ACTIONS.size());

    // Queries find tree actions by title, category and id.
    EXPECT(registry.matching_actions("branch"sv).size() == 5);
    EXPECT(registry.matching_actions("tree tabs"sv).size() == TREE_TABS_ACTIONS.size());
    EXPECT(registry.matching_actions("tree_tabs.indent"sv).size() == 1);
}

static void test_triggers_route_to_handler()
{
    ModuleRegistry registry;
    register_tree_tabs_module(registry);
    FakeHandler handler;
    register_tree_tabs_actions(registry, handler);

    auto const* close_branch = find_action(registry, "tree_tabs.close_branch"sv);
    EXPECT(close_branch);
    close_branch->trigger();
    find_action(registry, "tree_tabs.new_child_tab"sv)->trigger();

    EXPECT(handler.ran.size() == 2);
    EXPECT(handler.ran[0] == TreeTabsCommand::CloseBranch);
    EXPECT(handler.ran[1] == TreeTabsCommand::NewChildTab);
}

static void test_availability_follows_handler()
{
    ModuleRegistry registry;
    register_tree_tabs_module(registry);
    FakeHandler handler;
    register_tree_tabs_actions(registry, handler);

    handler.unavailable.set(static_cast<u8>(TreeTabsCommand::UnloadBranch));
    EXPECT(!find_action(registry, "tree_tabs.unload_branch"sv));
    EXPECT(registry.matching_actions().size() == TREE_TABS_ACTIONS.size() - 1);

    handler.unavailable.clear();
    EXPECT(find_action(registry, "tree_tabs.unload_branch"sv));
}

static void test_disabling_module_hides_actions()
{
    ModuleRegistry registry;
    register_tree_tabs_module(registry);
    FakeHandler handler;
    register_tree_tabs_actions(registry, handler);

    EXPECT(registry.set_enabled(TREE_TABS_MODULE_ID, false));
    EXPECT(registry.matching_actions().is_empty());

    EXPECT(registry.set_enabled(TREE_TABS_MODULE_ID, true));
    EXPECT(registry.matching_actions().size() == TREE_TABS_ACTIONS.size());

    registry.unregister_actions_for_module(TREE_TABS_MODULE_ID);
    EXPECT(registry.matching_actions().is_empty());
}

int main()
{
    test_action_table_is_well_formed();
    test_all_actions_register_on_the_bus();
    test_triggers_route_to_handler();
    test_availability_follows_handler();
    test_disabling_module_hides_actions();
    std::printf("All TreeTabsActions unit tests passed!\n");
    return 0;
}
