/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacesActions.h>

#include <algorithm>
#include <cstring>

namespace Superbird {

static constexpr Array<StringView, WORKSPACE_SWITCH_SLOTS> SWITCH_ACTION_IDS { {
    "workspaces.switch_1"sv, "workspaces.switch_2"sv, "workspaces.switch_3"sv, "workspaces.switch_4"sv, "workspaces.switch_5"sv,
    "workspaces.switch_6"sv, "workspaces.switch_7"sv, "workspaces.switch_8"sv, "workspaces.switch_9"sv,
} };

// The bus keeps titles as StringViews, so the switch actions' changing titles live in fixed buffers here.
static constexpr size_t TITLE_CAPACITY = 128;
static char s_switch_titles[WORKSPACE_SWITCH_SLOTS][TITLE_CAPACITY];

StringView workspace_switch_action_id(size_t slot)
{
    return SWITCH_ACTION_IDS[slot];
}

static void register_switch_action(ModuleRegistry& registry, WorkspacesCommandHandler& handler, size_t slot, StringView title)
{
    registry.register_action(ActionDescriptor {
        .id = SWITCH_ACTION_IDS[slot],
        .module_id = WORKSPACES_MODULE_ID,
        .title = title,
        .category = WORKSPACES_ACTION_CATEGORY,
        .is_available = [&handler, slot] { return handler.can_run(WorkspacesCommand::SwitchToWorkspace, slot); },
        .trigger = [&handler, slot] { handler.run(WorkspacesCommand::SwitchToWorkspace, slot); },
    });
}

static StringView write_title(size_t slot, std::string_view prefix, std::string_view name)
{
    auto* buffer = s_switch_titles[slot];
    auto prefix_length = std::min(prefix.size(), TITLE_CAPACITY);
    std::memcpy(buffer, prefix.data(), prefix_length);
    auto name_length = std::min(name.size(), TITLE_CAPACITY - prefix_length);
    // Don't cut a UTF-8 character in half.
    while (name_length > 0 && name_length < name.size() && (static_cast<unsigned char>(name[name_length]) & 0xC0) == 0x80)
        --name_length;
    std::memcpy(buffer + prefix_length, name.data(), name_length);
    return StringView { buffer, prefix_length + name_length };
}

void register_workspaces_actions(ModuleRegistry& registry, WorkspacesCommandHandler& handler)
{
    for (auto const& definition : WORKSPACES_ACTIONS) {
        auto command = definition.command;
        registry.register_action(ActionDescriptor {
            .id = definition.id,
            .module_id = WORKSPACES_MODULE_ID,
            .title = definition.title,
            .category = WORKSPACES_ACTION_CATEGORY,
            .is_available = [&handler, command] { return handler.can_run(command, 0); },
            .trigger = [&handler, command] { handler.run(command, 0); },
        });
    }
    set_workspace_switch_titles(registry, handler, {});
}

void set_workspace_switch_titles(ModuleRegistry& registry, WorkspacesCommandHandler& handler, std::span<std::string const> names)
{
    for (size_t slot = 0; slot < WORKSPACE_SWITCH_SLOTS; ++slot) {
        StringView title;
        if (slot < names.size()) {
            title = write_title(slot, "Switch to Workspace: ", names[slot]);
        } else {
            char number[2] = { static_cast<char>('1' + slot), 0 };
            title = write_title(slot, "Switch to Workspace ", number);
        }
        register_switch_action(registry, handler, slot, title);
    }
}

}
