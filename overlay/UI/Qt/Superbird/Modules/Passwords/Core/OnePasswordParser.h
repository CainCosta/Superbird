/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Superbird::Passwords {

// Qt-free parser for official 1Password CLI v2 (`op`) JSON outputs.
class OnePasswordParser {
public:
    // Parses stdout of `op item list --categories Login --format json`.
    static std::optional<std::vector<CredentialSummary>> parse_item_list(
        std::string_view json,
        std::string_view account_id = "1password");

    // Parses stdout of `op item get <id> --format json --reveal`.
    static std::optional<CredentialSecret> parse_item_detail(std::string_view json);

    // Parses stdout of `op item get <id> --otp` (trims whitespace, validates digits).
    static std::optional<std::string> parse_otp_output(std::string_view output);
};

}
