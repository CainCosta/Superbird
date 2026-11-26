/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenCrypto.h>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Superbird::Passwords {

// Platform OS keyring + biometric unlock helper (macOS Keychain + Touch ID; Linux Secret Service over D-Bus).
// Async load runs off the Qt GUI thread so Touch ID / Polkit prompts never stall the browser UI.
class Keyring {
public:
    using LoadCallback = std::function<void(std::optional<SecureBytes> secret, std::string error)>;

    static bool is_available();
    static bool supports_biometrics();

    static bool store_secret(
        std::string_view service,
        std::string_view account,
        std::span<uint8_t const> secret,
        bool require_biometrics = true);

    // Invokes callback on the Qt GUI thread.
    static void load_secret_async(
        std::string_view service,
        std::string_view account,
        std::string_view prompt_reason,
        LoadCallback callback);

    static bool has_secret(std::string_view service, std::string_view account);
    static bool delete_secret(std::string_view service, std::string_view account);
};

}
