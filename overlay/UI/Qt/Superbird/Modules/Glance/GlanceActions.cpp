/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/Glance/GlanceActions.h>

namespace Superbird {

void register_glance_actions(ModuleRegistry& registry, GlanceCommandHandler& handler)
{
    for (auto const& definition : GLANCE_ACTIONS) {
        auto command = definition.command;
        registry.register_action(ActionDescriptor {
            .id = definition.id,
            .module_id = GLANCE_MODULE_ID,
            .title = definition.title,
            .category = GLANCE_ACTION_CATEGORY,
            .is_available = [&handler, command] { return handler.can_run(command); },
            .trigger = [&handler, command] { handler.run(command); },
        });
    }
}

}
