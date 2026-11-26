/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Superbird::Passwords {

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::optional<uint16_t> port;
    std::string path;
    std::string normalized; // scheme://host[:port]/path...

    bool is_valid() const { return !scheme.empty() && !host.empty(); }
    uint16_t effective_port() const;
};

struct UriMatchResult {
    bool matched { false };
    int rank { 0 };
    UriMatchType matched_type { UriMatchType::Never };
};

class UriMatcher {
public:
    using RegistrableDomainResolver = std::function<std::optional<std::string>(std::string_view host)>;

    // Allows the browser runtime to plug in LibURL / libpsl registrable_domain().
    static void set_registrable_domain_resolver(RegistrableDomainResolver resolver);

    static std::optional<ParsedUrl> parse_url(std::string_view raw_url);
    static std::string registrable_domain(std::string_view host);
    static bool is_ip_address(std::string_view host);
    static bool is_autofillable_url(std::string_view raw_url);

    static UriMatchResult match_rule(std::string_view page_url, UriRule const& rule);
    static UriMatchResult match_credential(std::string_view page_url, CredentialSummary const& credential);

    // Returns matching credentials sorted by match quality (Exact > StartsWith > Host > Regex > BaseDomain),
    // with favorites boosted within the same tier and name tie-breaking.
    static std::vector<CredentialSummary> filter_and_rank(
        std::string_view page_url,
        std::vector<CredentialSummary> const& credentials);

    // Case-insensitive substring search across name, username, and URIs.
    static std::vector<CredentialSummary> search(
        std::string_view query,
        std::vector<CredentialSummary> const& credentials);
};

}
