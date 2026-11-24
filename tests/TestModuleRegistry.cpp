/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashMap.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>

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

struct FakeKey {
    StringView view;
    StringView bytes_as_string_view() const { return view; }
};

class RecordingObserver final : public ModuleRegistryObserver {
public:
    virtual void module_enabled_changed(StringView module_id, bool enabled) override
    {
        events.append({ module_id, enabled });
    }

    struct Event {
        StringView id;
        bool enabled;
    };
    Vector<Event> events;
};

static void test_module_registration_and_defaults()
{
    ModuleRegistry registry;

    EXPECT(registry.register_module({
        .id = "tree_tabs"sv,
        .name = "Tree-Style Tabs"sv,
        .description = "Nested tabs"sv,
        .enabled_by_default = true,
    }));
    // Duplicate IDs are rejected cleanly.
    EXPECT(!registry.register_module({
        .id = "tree_tabs"sv,
        .name = "Duplicate"sv,
        .description = ""sv,
        .enabled_by_default = false,
    }));
    EXPECT(registry.register_module({
        .id = "reader_view"sv,
        .name = "Reader View"sv,
        .description = "Clean article mode"sv,
        .enabled_by_default = false,
    }));

    EXPECT(registry.modules().size() == 2);
    EXPECT(registry.has_module("tree_tabs"sv));
    EXPECT(registry.is_enabled("tree_tabs"sv));
    EXPECT(!registry.is_enabled("reader_view"sv));
    EXPECT(!registry.is_enabled("unknown"sv));

    auto info = registry.module_info("reader_view"sv);
    EXPECT(info.has_value());
    EXPECT(info->name == "Reader View"sv);
}

static void test_enable_disable_and_observer_notifications()
{
    ModuleRegistry registry;
    registry.register_module({ .id = "tree_tabs"sv, .name = "Tree Tabs"sv, .description = ""sv, .enabled_by_default = true });
    registry.register_module({ .id = "glance"sv, .name = "Glance"sv, .description = ""sv, .enabled_by_default = false });

    RecordingObserver observer;
    registry.add_observer(observer);

    // No-op toggle does not notify.
    EXPECT(!registry.set_enabled("tree_tabs"sv, true));
    EXPECT(observer.events.is_empty());

    EXPECT(registry.set_enabled("tree_tabs"sv, false));
    EXPECT(!registry.is_enabled("tree_tabs"sv));
    EXPECT(observer.events.size() == 1);
    EXPECT(observer.events[0].id == "tree_tabs"sv && !observer.events[0].enabled);

    // Syncing overrides updates both modules and restores defaults for absent keys.
    struct OverrideEntry {
        FakeKey key;
        bool value;
    };
    Vector<OverrideEntry> overrides;
    overrides.append({ FakeKey { "glance"sv }, true });
    registry.sync_from_overrides(overrides);

    EXPECT(registry.is_enabled("tree_tabs"sv));
    EXPECT(registry.is_enabled("glance"sv));
    EXPECT(observer.events.size() == 3);

    registry.remove_observer(observer);
    EXPECT(registry.set_enabled("glance"sv, false));
    EXPECT(observer.events.size() == 3);
}

static void test_action_registration_and_filtering()
{
    ModuleRegistry registry;
    registry.register_module({ .id = "tree_tabs"sv, .name = "Tree Tabs"sv, .description = ""sv, .enabled_by_default = true });
    registry.register_module({ .id = "reader_view"sv, .name = "Reader View"sv, .description = ""sv, .enabled_by_default = false });

    int triggered = 0;
    bool child_available = true;

    registry.register_action({
        .id = "tree_tabs.new_child_tab"sv,
        .module_id = "tree_tabs"sv,
        .title = "New Child Tab"sv,
        .category = "Tree Tabs"sv,
        .is_available = [&] { return child_available; },
        .trigger = [&] { ++triggered; },
    });
    registry.register_action({
        .id = "reader_view.toggle"sv,
        .module_id = "reader_view"sv,
        .title = "Toggle Reader View"sv,
        .category = "Reading"sv,
        .trigger = [&] { triggered += 10; },
    });
    registry.register_action({
        .id = "global.settings"sv,
        .module_id = ""sv,
        .title = "Open Superbird Settings"sv,
        .category = "General"sv,
    });

    // Disabled module's action is excluded.
    auto all = registry.matching_actions();
    EXPECT(all.size() == 2);

    // Case-insensitive query filtering matches title, category, and id.
    auto child_matches = registry.matching_actions("child"sv);
    EXPECT(child_matches.size() == 1);
    EXPECT(child_matches[0]->id == "tree_tabs.new_child_tab"sv);
    child_matches[0]->trigger();
    EXPECT(triggered == 1);

    // Availability predicate dynamically hides unavailable actions.
    child_available = false;
    EXPECT(registry.matching_actions("child"sv).is_empty());
    child_available = true;

    // Enabling reader_view immediately exposes its action.
    registry.set_enabled("reader_view"sv, true);
    auto reader_matches = registry.matching_actions("READER"sv);
    EXPECT(reader_matches.size() == 1);
    reader_matches[0]->trigger();
    EXPECT(triggered == 11);

    // Unregistering removes only that module's actions.
    registry.unregister_actions_for_module("tree_tabs"sv);
    EXPECT(registry.matching_actions("child"sv).is_empty());
    EXPECT(registry.matching_actions("reader"sv).size() == 1);
}

int main()
{
    test_module_registration_and_defaults();
    test_enable_disable_and_observer_notifications();
    test_action_registration_and_filtering();
    std::printf("All ModuleRegistry unit tests passed!\n");
    return 0;
}
