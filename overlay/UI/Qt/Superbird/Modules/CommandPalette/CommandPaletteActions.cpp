/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/CommandPaletteActions.h>

namespace Superbird {

void register_browser_actions(ModuleRegistry& registry, BrowserCommandHandler& handler)
{
    for (auto const& definition : BROWSER_ACTIONS) {
        auto command = definition.command;
        registry.register_action(ActionDescriptor {
            .id = definition.id,
            .module_id = COMMAND_PALETTE_MODULE_ID,
            .title = definition.title,
            .category = BROWSER_ACTION_CATEGORY,
            .is_available = [&handler, command] { return handler.can_run(command); },
            .trigger = [&handler, command] { handler.run(command); },
        });
    }
}

static std::string to_std_string(StringView view)
{
    return std::string(view.characters_without_null_termination(), view.length());
}

std::vector<Palette::Entry> collect_action_entries(ModuleRegistry const& registry)
{
    std::vector<Palette::Entry> entries;
    for (auto const* action : registry.matching_actions()) {
        if (action->id == COMMAND_PALETTE_OPEN_ACTION_ID)
            continue;
        entries.push_back({
            .kind = Palette::EntryKind::Action,
            .title = to_std_string(action->title),
            .subtitle = to_std_string(action->category),
            .key = to_std_string(action->id),
        });
    }
    return entries;
}

bool trigger_action(ModuleRegistry const& registry, std::string_view action_id)
{
    StringView id { action_id.data(), action_id.size() };
    for (auto const* action : registry.matching_actions()) {
        if (action->id != id)
            continue;
        if (!action->trigger)
            return false;
        action->trigger();
        return true;
    }
    return false;
}

}
