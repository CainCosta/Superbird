/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenCrypto.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/Totp.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <cctype>
#include <cmath>
#include <cstring>

namespace Superbird::Passwords {

namespace {

int hex_val(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::string percent_decode(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hex_val(s[i + 1]);
            int lo = hex_val(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i] == '+' ? ' ' : s[i]);
    }
    return out;
}

std::string ascii_upper(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return out;
}

} // namespace

std::optional<std::vector<uint8_t>> Totp::base32_decode(std::string_view input)
{
    std::vector<uint8_t> out;
    uint32_t buffer = 0;
    int bits_left = 0;
    bool seen_pad = false;

    for (char raw : input) {
        if (raw == ' ' || raw == '-' || raw == '\t' || raw == '\r' || raw == '\n')
            continue;
        if (raw == '=') {
            seen_pad = true;
            continue;
        }
        if (seen_pad)
            return std::nullopt;

        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
        int val = -1;
        if (c >= 'A' && c <= 'Z')
            val = c - 'A';
        else if (c >= '2' && c <= '7')
            val = c - '2' + 26;
        else
            return std::nullopt;

        buffer = (buffer << 5) | static_cast<uint32_t>(val);
        bits_left += 5;
        if (bits_left >= 8) {
            bits_left -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits_left) & 0xFF));
        }
    }

    if (out.empty())
        return std::nullopt;
    return out;
}

std::optional<TotpConfig> Totp::parse(std::string_view uri_or_secret)
{
    while (!uri_or_secret.empty() && std::isspace(static_cast<unsigned char>(uri_or_secret.front())))
        uri_or_secret.remove_prefix(1);
    while (!uri_or_secret.empty() && std::isspace(static_cast<unsigned char>(uri_or_secret.back())))
        uri_or_secret.remove_suffix(1);
    if (uri_or_secret.empty())
        return std::nullopt;

    constexpr std::string_view OTPAUTH_PREFIX = "otpauth://";
    if (uri_or_secret.size() > OTPAUTH_PREFIX.size()
        && ascii_upper(uri_or_secret.substr(0, OTPAUTH_PREFIX.size())) == "OTPAUTH://") {
        auto rest = uri_or_secret.substr(OTPAUTH_PREFIX.size());
        auto slash = rest.find('/');
        if (slash == std::string_view::npos)
            return std::nullopt;
        auto type = ascii_upper(rest.substr(0, slash));
        if (type != "TOTP")
            return std::nullopt;

        auto after_type = rest.substr(slash + 1);
        auto qmark = after_type.find('?');
        if (qmark == std::string_view::npos)
            return std::nullopt;

        auto label = percent_decode(after_type.substr(0, qmark));
        auto query = after_type.substr(qmark + 1);

        TotpConfig config;
        if (auto colon = label.find(':'); colon != std::string::npos) {
            config.issuer = label.substr(0, colon);
            config.account_name = label.substr(colon + 1);
            while (!config.account_name.empty() && config.account_name.front() == ' ')
                config.account_name.erase(config.account_name.begin());
        } else {
            config.account_name = label;
        }

        std::string secret_str;
        size_t pos = 0;
        while (pos < query.size()) {
            auto amp = query.find('&', pos);
            auto pair = (amp == std::string_view::npos) ? query.substr(pos) : query.substr(pos, amp - pos);
            if (auto eq = pair.find('='); eq != std::string_view::npos) {
                auto key = ascii_upper(pair.substr(0, eq));
                auto val = percent_decode(pair.substr(eq + 1));
                if (key == "SECRET") {
                    secret_str = std::move(val);
                } else if (key == "ISSUER") {
                    config.issuer = std::move(val);
                } else if (key == "ALGORITHM") {
                    auto alg = ascii_upper(val);
                    if (alg == "SHA1" || alg == "SHA-1")
                        config.algorithm = TotpAlgorithm::Sha1;
                    else if (alg == "SHA256" || alg == "SHA-256")
                        config.algorithm = TotpAlgorithm::Sha256;
                    else if (alg == "SHA512" || alg == "SHA-512")
                        config.algorithm = TotpAlgorithm::Sha512;
                    else
                        return std::nullopt;
                } else if (key == "DIGITS") {
                    uint32_t d = 0;
                    for (char c : val) {
                        if (c < '0' || c > '9')
                            return std::nullopt;
                        d = d * 10 + static_cast<uint32_t>(c - '0');
                    }
                    if (d < 6 || d > 8)
                        return std::nullopt;
                    config.digits = d;
                } else if (key == "PERIOD") {
                    uint32_t p = 0;
                    for (char c : val) {
                        if (c < '0' || c > '9')
                            return std::nullopt;
                        p = p * 10 + static_cast<uint32_t>(c - '0');
                    }
                    if (p == 0 || p > 3600)
                        return std::nullopt;
                    config.period = p;
                }
            }
            if (amp == std::string_view::npos)
                break;
            pos = amp + 1;
        }

        auto decoded = base32_decode(secret_str);
        cleanse_string(secret_str);
        if (!decoded)
            return std::nullopt;
        config.secret = std::move(*decoded);
        return config;
    }

    auto decoded = base32_decode(uri_or_secret);
    if (!decoded)
        return std::nullopt;
    TotpConfig config;
    config.secret = std::move(*decoded);
    return config;
}

std::optional<TotpCode> Totp::generate(TotpConfig const& config, int64_t unix_timestamp_seconds)
{
    if (config.secret.empty() || config.period == 0 || config.digits < 6 || config.digits > 8 || unix_timestamp_seconds < 0)
        return std::nullopt;

    uint64_t counter = static_cast<uint64_t>(unix_timestamp_seconds) / config.period;
    uint8_t msg[8];
    for (int i = 7; i >= 0; --i) {
        msg[i] = static_cast<uint8_t>(counter & 0xFF);
        counter >>= 8;
    }

    EVP_MD const* md = EVP_sha1();
    switch (config.algorithm) {
    case TotpAlgorithm::Sha1:
        md = EVP_sha1();
        break;
    case TotpAlgorithm::Sha256:
        md = EVP_sha256();
        break;
    case TotpAlgorithm::Sha512:
        md = EVP_sha512();
        break;
    }

    uint8_t digest[EVP_MAX_MD_SIZE] {};
    unsigned int digest_len = 0;
    if (!HMAC(md, config.secret.data(), static_cast<int>(config.secret.size()), msg, sizeof(msg), digest, &digest_len)
        || digest_len < 20)
        return std::nullopt;

    size_t offset = digest[digest_len - 1] & 0x0F;
    if (offset + 4 > digest_len)
        return std::nullopt;

    uint32_t bin_code = (static_cast<uint32_t>(digest[offset] & 0x7F) << 24)
        | (static_cast<uint32_t>(digest[offset + 1]) << 16)
        | (static_cast<uint32_t>(digest[offset + 2]) << 8)
        | static_cast<uint32_t>(digest[offset + 3]);

    cleanse_memory(digest, sizeof(digest));

    uint32_t modulo = 1;
    for (uint32_t i = 0; i < config.digits; ++i)
        modulo *= 10;

    uint32_t otp = bin_code % modulo;
    std::string code(config.digits, '0');
    for (int i = static_cast<int>(config.digits) - 1; i >= 0; --i) {
        code[static_cast<size_t>(i)] = static_cast<char>('0' + (otp % 10));
        otp /= 10;
    }

    uint32_t elapsed = static_cast<uint32_t>(static_cast<uint64_t>(unix_timestamp_seconds) % config.period);
    uint32_t remaining = config.period - elapsed;
    return TotpCode { std::move(code), remaining, config.period };
}

std::optional<TotpCode> Totp::generate_from_uri(std::string_view uri_or_secret, int64_t unix_timestamp_seconds)
{
    auto parsed = parse(uri_or_secret);
    if (!parsed)
        return std::nullopt;
    auto code = generate(*parsed, unix_timestamp_seconds);
    cleanse_memory(parsed->secret.data(), parsed->secret.size());
    return code;
}

}
