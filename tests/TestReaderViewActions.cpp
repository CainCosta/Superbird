/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashTable.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ReaderViewActions.h>

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

class FakeHandler final : public ReaderCommandHandler {
public:
    virtual bool can_run(ReaderCommand command) const override { return reader_open || command == ReaderCommand::Toggle; }
    virtual void run(ReaderCommand command) override { ran.append(command); }
    bool reader_open { false };
    Vector<ReaderCommand> ran;
};

int main()
{
    HashTable<StringView> ids;
    for (auto const& definition : READER_VIEW_ACTIONS) {
        EXPECT(definition.id.length() > 12 && definition.id.substring_view(0, 12) == "reader_view."sv);
        EXPECT(!definition.title.is_empty());
        EXPECT(ids.set(definition.id) == HashSetResult::InsertedNewEntry);
    }

    ModuleRegistry registry;
    EXPECT(registry.register_module({ .id = READER_VIEW_MODULE_ID, .name = "Reader View"sv, .description = ""sv, .enabled_by_default = true }));
    FakeHandler handler;
    register_reader_view_actions(registry, handler);

    // Only the toggle is offered until a reader is open.
    auto actions = registry.matching_actions();
    EXPECT(actions.size() == 1 && actions[0]->id == "reader_view.toggle"sv);
    EXPECT(actions[0]->shortcut.has_value());
    handler.reader_open = true;
    EXPECT(registry.matching_actions().size() == READER_VIEW_ACTIONS.size());
    EXPECT(registry.matching_actions("larger"sv).size() == 1);

    for (auto const* action : registry.matching_actions("theme"sv))
        action->trigger();
    EXPECT(handler.ran.size() == 1 && handler.ran[0] == ReaderCommand::CycleTheme);

    registry.set_enabled(READER_VIEW_MODULE_ID, false);
    EXPECT(registry.matching_actions().is_empty());

    std::printf("All ReaderViewActions unit tests passed!\n");
    return 0;
}
