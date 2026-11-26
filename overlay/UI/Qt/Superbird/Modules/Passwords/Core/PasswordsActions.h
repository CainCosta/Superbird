/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Array.h>
#include <AK/StringView.h>
#include <AK/Types.h>

namespace Superbird {

class ModuleRegistry;

inline constexpr StringView PASSWORDS_MODULE_ID = "passwords"sv;

// The Password Manager commands on the Superbird action bus. Kept free of Qt so the table and its registration are
// unit-testable on their own.
enum class PasswordsCommand : u8 {
    AutofillLogin,
    OpenPopover,
    SearchPasswords,
    CopyTotp,
    GeneratePassword,
    SyncVaults,
    LockAllVaults,
};

struct PasswordsActionDefinition {
    PasswordsCommand command;
    StringView id;
    StringView title;
};

inline constexpr StringView PASSWORDS_ACTION_CATEGORY = "Passwords"sv;

inline constexpr Array<PasswordsActionDefinition, 7> PASSWORDS_ACTIONS { {
    { PasswordsCommand::AutofillLogin, "passwords.autofill"sv, "Passwords: Autofill Login"sv },
    { PasswordsCommand::OpenPopover, "passwords.open"sv, "Passwords: Open Password Manager"sv },
    { PasswordsCommand::SearchPasswords, "passwords.search"sv, "Passwords: Search Passwords"sv },
    { PasswordsCommand::CopyTotp, "passwords.copy_totp"sv, "Passwords: Copy TOTP for Current Site"sv },
    { PasswordsCommand::GeneratePassword, "passwords.generate"sv, "Passwords: Generate Strong Password"sv },
    { PasswordsCommand::SyncVaults, "passwords.sync"sv, "Passwords: Sync Vaults"sv },
    { PasswordsCommand::LockAllVaults, "passwords.lock"sv, "Passwords: Lock All Vaults"sv },
} };

class PasswordsCommandHandler {
public:
    virtual ~PasswordsCommandHandler() = default;
    virtual bool can_run(PasswordsCommand) const = 0;
    virtual void run(PasswordsCommand) = 0;
};

void register_passwords_actions(ModuleRegistry&, PasswordsCommandHandler& handler);

}
