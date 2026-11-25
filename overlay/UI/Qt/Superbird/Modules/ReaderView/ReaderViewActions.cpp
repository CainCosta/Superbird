/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Platform.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ReaderViewActions.h>

namespace Superbird {

#if defined(AK_OS_MACOS)
// NB: Cmd+Option+R and Cmd+Shift+R already reload bypassing the cache on macOS.
static constexpr StringView TOGGLE_SHORTCUT = "Ctrl+Cmd+R"sv;
#else
static constexpr StringView TOGGLE_SHORTCUT = "Ctrl+Alt+R"sv;
#endif

void register_reader_view_actions(ModuleRegistry& registry, ReaderCommandHandler& handler)
{
    for (auto const& definition : READER_VIEW_ACTIONS) {
        auto command = definition.command;
        registry.register_action(ActionDescriptor {
            .id = definition.id,
            .module_id = READER_VIEW_MODULE_ID,
            .title = definition.title,
            .category = READER_VIEW_ACTION_CATEGORY,
            .shortcut = command == ReaderCommand::Toggle ? Optional<StringView> { TOGGLE_SHORTCUT } : Optional<StringView> {},
            .is_available = [&handler, command] { return handler.can_run(command); },
            .trigger = [&handler, command] { handler.run(command); },
        });
    }
}

}
