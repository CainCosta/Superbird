/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabsActions.h>

namespace Superbird {

void register_tree_tabs_actions(ModuleRegistry& registry, TreeTabsCommandHandler& handler)
{
    for (auto const& definition : TREE_TABS_ACTIONS) {
        auto command = definition.command;
        registry.register_action(ActionDescriptor {
            .id = definition.id,
            .module_id = TREE_TABS_MODULE_ID,
            .title = definition.title,
            .category = TREE_TABS_ACTION_CATEGORY,
            .is_available = [&handler, command] { return handler.can_run(command); },
            .trigger = [&handler, command] { handler.run(command); },
        });
    }
}

}
