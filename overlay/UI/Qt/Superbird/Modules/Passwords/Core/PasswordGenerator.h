/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Superbird::Passwords {

struct PasswordGeneratorOptions {
    size_t length { 20 };
    bool uppercase { true };
    bool lowercase { true };
    bool digits { true };
    bool symbols { true };
    bool avoid_ambiguous { false };
};

struct PassphraseGeneratorOptions {
    size_t word_count { 4 };
    char separator { '-' };
    bool capitalize { true };
    bool include_number { true };
};

struct PasswordStrength {
    double entropy_bits { 0.0 };
    std::string_view label { "Weak" };
};

class PasswordGenerator {
public:
    static std::string generate_password(PasswordGeneratorOptions const& options = {});
    static std::string generate_passphrase(PassphraseGeneratorOptions const& options = {});
    static PasswordStrength estimate_strength(std::string_view password);
};

}
