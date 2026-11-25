/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitViewActions.h>

namespace Superbird {

void register_split_view_actions(ModuleRegistry& registry, SplitCommandHandler& handler)
{
    for (auto const& definition : SPLIT_VIEW_ACTIONS) {
        auto command = definition.command;
        registry.register_action(ActionDescriptor {
            .id = definition.id,
            .module_id = SPLIT_VIEW_MODULE_ID,
            .title = definition.title,
            .category = SPLIT_VIEW_ACTION_CATEGORY,
            .is_available = [&handler, command] { return handler.can_run(command); },
            .trigger = [&handler, command] { handler.run(command); },
        });
    }
}

}
