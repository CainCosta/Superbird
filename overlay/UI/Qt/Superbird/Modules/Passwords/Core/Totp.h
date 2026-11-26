/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Superbird::Passwords {

enum class TotpAlgorithm : uint8_t {
    Sha1 = 0,
    Sha256 = 1,
    Sha512 = 2,
};

struct TotpConfig {
    std::vector<uint8_t> secret;
    TotpAlgorithm algorithm { TotpAlgorithm::Sha1 };
    uint32_t digits { 6 };
    uint32_t period { 30 };
    std::string issuer;
    std::string account_name;
};

struct TotpCode {
    std::string code;
    uint32_t seconds_remaining { 30 };
    uint32_t period { 30 };
};

class Totp {
public:
    static std::optional<std::vector<uint8_t>> base32_decode(std::string_view input);

    // Parses either an `otpauth://totp/...?...` URI or a raw Base32 secret string.
    static std::optional<TotpConfig> parse(std::string_view uri_or_secret);

    static std::optional<TotpCode> generate(TotpConfig const& config, int64_t unix_timestamp_seconds);
    static std::optional<TotpCode> generate_from_uri(std::string_view uri_or_secret, int64_t unix_timestamp_seconds);
};

}
