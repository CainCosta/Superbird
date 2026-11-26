/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/UriMatcher.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <regex.h>
#include <string>

namespace Superbird::Passwords {

namespace {

UriMatcher::RegistrableDomainResolver& custom_resolver()
{
    static UriMatcher::RegistrableDomainResolver resolver;
    return resolver;
}

std::string ascii_lower(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for (char c : input)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool icontains(std::string_view haystack, std::string_view needle_lower)
{
    if (needle_lower.empty())
        return true;
    if (needle_lower.size() > haystack.size())
        return false;
    auto h = ascii_lower(haystack);
    return h.find(needle_lower) != std::string::npos;
}

// Common multi-label public suffixes used as fallback when no libpsl resolver is registered (e.g., standalone unit tests).
bool is_known_multipart_public_suffix(std::string_view two_labels)
{
    static constexpr std::array<std::string_view, 32> SUFFIXES = {
        "co.uk", "org.uk", "ac.uk", "gov.uk", "me.uk", "net.uk",
        "com.au", "net.au", "org.au", "edu.au", "gov.au",
        "co.jp", "ne.jp", "or.jp", "ac.jp", "go.jp",
        "com.br", "net.br", "org.br", "gov.br",
        "co.nz", "net.nz", "org.nz",
        "co.in", "net.in", "org.in",
        "com.mx", "com.cn", "com.sg", "com.tw",
        "co.za", "github.io",
    };
    return std::find(SUFFIXES.begin(), SUFFIXES.end(), two_labels) != SUFFIXES.end();
}

int base_rank_for_type(UriMatchType type)
{
    switch (type) {
    case UriMatchType::Exact:
        return 400;
    case UriMatchType::StartsWith:
        return 300;
    case UriMatchType::Host:
        return 200;
    case UriMatchType::RegularExpression:
        return 150;
    case UriMatchType::BaseDomain:
        return 100;
    case UriMatchType::Never:
        return 0;
    }
    return 0;
}

} // namespace

uint16_t ParsedUrl::effective_port() const
{
    if (port.has_value())
        return *port;
    if (scheme == "https")
        return 443;
    if (scheme == "http")
        return 80;
    return 0;
}

void UriMatcher::set_registrable_domain_resolver(RegistrableDomainResolver resolver)
{
    custom_resolver() = std::move(resolver);
}

bool UriMatcher::is_ip_address(std::string_view host)
{
    if (host.empty())
        return false;
    if (host.front() == '[' && host.back() == ']')
        return true;
    if (host.find(':') != std::string_view::npos)
        return true;

    int dots = 0;
    int octet = 0;
    int digits = 0;
    for (char c : host) {
        if (c == '.') {
            if (digits == 0 || octet > 255)
                return false;
            ++dots;
            octet = 0;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            octet = octet * 10 + (c - '0');
            ++digits;
            if (digits > 3 || octet > 255)
                return false;
        } else {
            return false;
        }
    }
    return dots == 3 && digits > 0 && octet <= 255;
}

std::optional<ParsedUrl> UriMatcher::parse_url(std::string_view raw_url)
{
    auto s = trim(raw_url);
    if (s.empty())
        return std::nullopt;

    std::string owned;
    auto scheme_pos = s.find("://");
    if (scheme_pos == std::string_view::npos) {
        // Bare domain like "github.com" or "example.com/login" — treat as https:// for URI rules.
        if (s.find(':') != std::string_view::npos && s.find('/') == std::string_view::npos) {
            // Could be host:port (like localhost:8080) or non-http scheme (like androidapp:// or mailto:).
            auto colon = s.find(':');
            auto after = s.substr(colon + 1);
            bool all_digits = !after.empty() && std::all_of(after.begin(), after.end(), [](char c) { return c >= '0' && c <= '9'; });
            if (!all_digits)
                return std::nullopt;
        }
        owned = "https://" + std::string(s);
        s = owned;
        scheme_pos = 5;
    }

    ParsedUrl out;
    out.scheme = ascii_lower(s.substr(0, scheme_pos));
    auto rest = s.substr(scheme_pos + 3);
    if (rest.empty())
        return std::nullopt;

    // Split authority vs path/query/fragment.
    size_t path_start = rest.find_first_of("/?#");
    std::string_view authority = (path_start == std::string_view::npos) ? rest : rest.substr(0, path_start);
    std::string_view remainder = (path_start == std::string_view::npos) ? std::string_view {} : rest.substr(path_start);

    // Strip userinfo if present.
    if (auto at = authority.rfind('@'); at != std::string_view::npos)
        authority = authority.substr(at + 1);
    if (authority.empty())
        return std::nullopt;

    std::string_view host_part = authority;
    std::string_view port_part;
    if (authority.front() == '[') {
        auto close_bracket = authority.find(']');
        if (close_bracket == std::string_view::npos)
            return std::nullopt;
        host_part = authority.substr(0, close_bracket + 1);
        if (close_bracket + 1 < authority.size()) {
            if (authority[close_bracket + 1] != ':')
                return std::nullopt;
            port_part = authority.substr(close_bracket + 2);
        }
    } else if (auto colon = authority.rfind(':'); colon != std::string_view::npos) {
        host_part = authority.substr(0, colon);
        port_part = authority.substr(colon + 1);
    }

    out.host = ascii_lower(host_part);
    while (!out.host.empty() && out.host.back() == '.')
        out.host.pop_back();
    if (out.host.empty())
        return std::nullopt;

    if (!port_part.empty()) {
        uint32_t p = 0;
        for (char c : port_part) {
            if (c < '0' || c > '9')
                return std::nullopt;
            p = p * 10 + static_cast<uint32_t>(c - '0');
            if (p > 65535)
                return std::nullopt;
        }
        if (p == 0)
            return std::nullopt;
        out.port = static_cast<uint16_t>(p);
    }

    if (remainder.empty()) {
        out.path = "/";
    } else if (remainder.front() != '/') {
        out.path = "/" + std::string(remainder);
    } else {
        out.path = std::string(remainder);
    }

    out.normalized = out.scheme + "://" + out.host;
    if (out.port.has_value()) {
        bool is_default = (out.scheme == "https" && *out.port == 443) || (out.scheme == "http" && *out.port == 80);
        if (!is_default)
            out.normalized += ":" + std::to_string(*out.port);
    }
    out.normalized += out.path;
    return out;
}

std::string UriMatcher::registrable_domain(std::string_view host)
{
    auto lower = ascii_lower(host);
    while (!lower.empty() && lower.back() == '.')
        lower.pop_back();
    if (lower.empty() || is_ip_address(lower) || lower == "localhost")
        return lower;

    if (auto& resolver = custom_resolver()) {
        if (auto resolved = resolver(lower); resolved.has_value() && !resolved->empty())
            return ascii_lower(*resolved);
    }

    std::vector<std::string_view> labels;
    size_t pos = 0;
    while (pos <= lower.size()) {
        auto dot = lower.find('.', pos);
        if (dot == std::string::npos) {
            labels.push_back(std::string_view(lower).substr(pos));
            break;
        }
        labels.push_back(std::string_view(lower).substr(pos, dot - pos));
        pos = dot + 1;
    }

    if (labels.size() <= 2)
        return lower;

    std::string last_two = std::string(labels[labels.size() - 2]) + "." + std::string(labels[labels.size() - 1]);
    if (is_known_multipart_public_suffix(last_two)) {
        if (labels.size() >= 3)
            return std::string(labels[labels.size() - 3]) + "." + last_two;
        return last_two;
    }
    return last_two;
}

bool UriMatcher::is_autofillable_url(std::string_view raw_url)
{
    auto parsed = parse_url(raw_url);
    if (!parsed)
        return false;
    return parsed->scheme == "http" || parsed->scheme == "https";
}

UriMatchResult UriMatcher::match_rule(std::string_view page_url, UriRule const& rule)
{
    if (rule.match_type == UriMatchType::Never || rule.uri.empty())
        return {};

    auto page = parse_url(page_url);
    if (!page)
        return {};

    switch (rule.match_type) {
    case UriMatchType::Never:
        return {};

    case UriMatchType::RegularExpression: {
        // Bound regex length to avoid pathological compilation; use POSIX ERE (zero C++ exceptions).
        if (rule.uri.size() > 512)
            return {};
        regex_t re {};
        if (regcomp(&re, rule.uri.c_str(), REG_EXTENDED | REG_ICASE | REG_NOSUB) != 0)
            return {};
        std::string raw_str(page_url);
        bool matched = (regexec(&re, raw_str.c_str(), 0, nullptr, 0) == 0)
            || (regexec(&re, page->normalized.c_str(), 0, nullptr, 0) == 0);
        regfree(&re);
        if (matched)
            return { true, base_rank_for_type(UriMatchType::RegularExpression), UriMatchType::RegularExpression };
        return {};
    }

    case UriMatchType::StartsWith: {
        auto rule_parsed = parse_url(rule.uri);
        std::string prefix = rule_parsed ? rule_parsed->normalized : std::string(trim(rule.uri));
        // Strip trailing slash only if rule didn't specify a path beyond root.
        auto raw_trimmed = trim(rule.uri);
        if (page->normalized.starts_with(prefix) || page_url.starts_with(raw_trimmed))
            return { true, base_rank_for_type(UriMatchType::StartsWith), UriMatchType::StartsWith };
        return {};
    }

    case UriMatchType::Exact: {
        auto rule_parsed = parse_url(rule.uri);
        if (rule_parsed && page->normalized == rule_parsed->normalized)
            return { true, base_rank_for_type(UriMatchType::Exact), UriMatchType::Exact };
        if (trim(page_url) == trim(rule.uri))
            return { true, base_rank_for_type(UriMatchType::Exact), UriMatchType::Exact };
        return {};
    }

    case UriMatchType::Host: {
        auto rule_parsed = parse_url(rule.uri);
        if (!rule_parsed)
            return {};
        if (page->host != rule_parsed->host)
            return {};
        if (rule_parsed->port.has_value() && page->effective_port() != rule_parsed->effective_port())
            return {};
        return { true, base_rank_for_type(UriMatchType::Host), UriMatchType::Host };
    }

    case UriMatchType::BaseDomain: {
        auto rule_parsed = parse_url(rule.uri);
        if (!rule_parsed)
            return {};
        // Only allow http/https cross-matching when both are web schemes, otherwise require same scheme.
        bool both_web = (page->scheme == "http" || page->scheme == "https")
            && (rule_parsed->scheme == "http" || rule_parsed->scheme == "https");
        if (!both_web && page->scheme != rule_parsed->scheme)
            return {};

        auto page_domain = registrable_domain(page->host);
        auto rule_domain = registrable_domain(rule_parsed->host);
        if (page_domain.empty() || page_domain != rule_domain)
            return {};

        // Slightly boost exact-host matches within BaseDomain tier.
        int rank = base_rank_for_type(UriMatchType::BaseDomain);
        if (page->host == rule_parsed->host)
            rank += 10;
        return { true, rank, UriMatchType::BaseDomain };
    }
    }
    return {};
}

UriMatchResult UriMatcher::match_credential(std::string_view page_url, CredentialSummary const& credential)
{
    UriMatchResult best;
    for (auto const& rule : credential.uris) {
        auto res = match_rule(page_url, rule);
        if (res.matched && (!best.matched || res.rank > best.rank))
            best = res;
    }
    return best;
}

std::vector<CredentialSummary> UriMatcher::filter_and_rank(
    std::string_view page_url,
    std::vector<CredentialSummary> const& credentials)
{
    struct Scored {
        CredentialSummary summary;
        int rank;
    };

    std::vector<Scored> matches;
    for (auto const& cred : credentials) {
        auto res = match_credential(page_url, cred);
        if (!res.matched)
            continue;
        int score = res.rank * 10 + (cred.favorite ? 5 : 0);
        matches.push_back({ cred, score });
    }

    std::stable_sort(matches.begin(), matches.end(), [](Scored const& a, Scored const& b) {
        if (a.rank != b.rank)
            return a.rank > b.rank;
        return ascii_lower(a.summary.name) < ascii_lower(b.summary.name);
    });

    std::vector<CredentialSummary> out;
    out.reserve(matches.size());
    for (auto& m : matches)
        out.push_back(std::move(m.summary));
    return out;
}

std::vector<CredentialSummary> UriMatcher::search(
    std::string_view query,
    std::vector<CredentialSummary> const& credentials)
{
    auto q = ascii_lower(trim(query));
    std::vector<CredentialSummary> out;
    for (auto const& cred : credentials) {
        if (q.empty()) {
            out.push_back(cred);
            continue;
        }
        bool matched = icontains(cred.name, q) || icontains(cred.username, q);
        if (!matched) {
            for (auto const& rule : cred.uris) {
                if (icontains(rule.uri, q)) {
                    matched = true;
                    break;
                }
            }
        }
        if (matched)
            out.push_back(cred);
    }

    std::stable_sort(out.begin(), out.end(), [](CredentialSummary const& a, CredentialSummary const& b) {
        if (a.favorite != b.favorite)
            return a.favorite > b.favorite;
        return ascii_lower(a.name) < ascii_lower(b.name);
    });
    return out;
}

}
