/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Platform.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordsActions.h>

namespace Superbird {

#if defined(AK_OS_MACOS)
static constexpr StringView AUTOFILL_SHORTCUT = "Cmd+Shift+L"sv;
#else
static constexpr StringView AUTOFILL_SHORTCUT = "Ctrl+Shift+L"sv;
#endif

void register_passwords_actions(ModuleRegistry& registry, PasswordsCommandHandler& handler)
{
    for (auto const& definition : PASSWORDS_ACTIONS) {
        auto command = definition.command;
        registry.register_action(ActionDescriptor {
            .id = definition.id,
            .module_id = PASSWORDS_MODULE_ID,
            .title = definition.title,
            .category = PASSWORDS_ACTION_CATEGORY,
            .shortcut = command == PasswordsCommand::AutofillLogin ? Optional<StringView> { AUTOFILL_SHORTCUT } : Optional<StringView> {},
            .is_available = [&handler, command] { return handler.can_run(command); },
            .trigger = [&handler, command] { handler.run(command); },
        });
    }
}

}
