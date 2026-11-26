/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Superbird::ContentBlockingFormat {

inline constexpr int64_t minimum_expiry_seconds = 60 * 60;
inline constexpr int64_t maximum_expiry_seconds = 14 * 24 * 60 * 60;
inline constexpr int64_t default_expiry_seconds = 24 * 60 * 60;
inline constexpr int64_t quick_fixes_default_expiry_seconds = 4 * 60 * 60;

struct ListEntry {
    std::string id;
    bool trusted { false };
    bool is_hosts_format { false };
    std::string_view contents;
};

inline bool is_trusted_list_id(std::string_view id)
{
    return id == "uboFilters"
        || id == "uboBadware"
        || id == "uboPrivacy"
        || id == "uboUnbreak"
        || id == "uboQuickFixes";
}

inline bool is_ubo_only_list_id(std::string_view id)
{
    return is_trusted_list_id(id) || id == "peterLowe" || id == "urlhaus";
}

inline std::string_view trim_ascii_whitespace(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

inline bool equals_ignore_case(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

inline std::optional<int64_t> parse_duration_seconds(std::string_view value)
{
    auto trimmed = trim_ascii_whitespace(value);
    if (trimmed.empty())
        return std::nullopt;

    size_t digit_end = 0;
    while (digit_end < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[digit_end])))
        ++digit_end;
    if (digit_end == 0)
        return std::nullopt;

    int64_t amount = 0;
    for (size_t i = 0; i < digit_end; ++i) {
        int digit = trimmed[i] - '0';
        if (amount > (maximum_expiry_seconds / 60) + 1) {
            amount = (maximum_expiry_seconds / 60) + 1;
            break;
        }
        amount = amount * 10 + digit;
    }
    if (amount <= 0)
        return std::nullopt;

    auto rest = trim_ascii_whitespace(trimmed.substr(digit_end));
    if (rest.empty())
        return std::nullopt;

    size_t unit_end = 0;
    while (unit_end < rest.size() && std::isalpha(static_cast<unsigned char>(rest[unit_end])))
        ++unit_end;
    if (unit_end == 0)
        return std::nullopt;

    auto unit = rest.substr(0, unit_end);
    int64_t multiplier = 0;
    if (equals_ignore_case(unit, "day") || equals_ignore_case(unit, "days") || equals_ignore_case(unit, "d"))
        multiplier = 24 * 60 * 60;
    else if (equals_ignore_case(unit, "hour") || equals_ignore_case(unit, "hours") || equals_ignore_case(unit, "hr") || equals_ignore_case(unit, "hrs") || equals_ignore_case(unit, "h"))
        multiplier = 60 * 60;
    else if (equals_ignore_case(unit, "minute") || equals_ignore_case(unit, "minutes") || equals_ignore_case(unit, "min") || equals_ignore_case(unit, "mins") || equals_ignore_case(unit, "m"))
        multiplier = 60;
    else
        return std::nullopt;

    if (amount > maximum_expiry_seconds / multiplier)
        return maximum_expiry_seconds;

    int64_t seconds = amount * multiplier;
    if (seconds < minimum_expiry_seconds)
        return minimum_expiry_seconds;
    if (seconds > maximum_expiry_seconds)
        return maximum_expiry_seconds;
    return seconds;
}

inline int64_t parse_list_expiry_seconds(std::string_view header_text, std::string_view identifier = {})
{
    if (header_text.size() > 2048)
        header_text = header_text.substr(0, 2048);

    std::optional<int64_t> smallest_expiry;
    size_t pos = 0;
    while (pos < header_text.size()) {
        size_t newline = header_text.find('\n', pos);
        size_t line_end = newline == std::string_view::npos ? header_text.size() : newline;
        auto line = trim_ascii_whitespace(header_text.substr(pos, line_end - pos));
        pos = newline == std::string_view::npos ? header_text.size() : newline + 1;

        if (line.empty() || line.front() == '[')
            continue;
        if (line.front() != '!')
            break;

        auto body = trim_ascii_whitespace(line.substr(1));
        std::string_view duration_str;
        if (body.size() > 8 && equals_ignore_case(body.substr(0, 8), "Expires:"))
            duration_str = body.substr(8);
        else if (body.size() > 13 && equals_ignore_case(body.substr(0, 13), "Diff-Expires:"))
            duration_str = body.substr(13);
        else
            continue;

        if (auto parsed = parse_duration_seconds(duration_str); parsed.has_value()) {
            if (!smallest_expiry.has_value() || *parsed < *smallest_expiry)
                smallest_expiry = *parsed;
        }
    }

    if (smallest_expiry.has_value())
        return *smallest_expiry;
    if (identifier == "uboQuickFixes")
        return quick_fixes_default_expiry_seconds;
    return default_expiry_seconds;
}

inline std::string sanitize_header_id(std::string_view id)
{
    std::string out;
    out.reserve(id.size());
    for (unsigned char c : id) {
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.')
            out.push_back(static_cast<char>(c));
        else
            out.push_back('_');
    }
    if (out.empty())
        return "list";
    return out;
}

inline std::string build_site_switches_rules(std::vector<std::string> const& no_cosmetic_hosts)
{
    std::string out;
    for (auto const& raw_host : no_cosmetic_hosts) {
        auto host = trim_ascii_whitespace(raw_host);
        if (host.empty())
            continue;
        bool valid = true;
        for (unsigned char c : host) {
            if (!(std::isalnum(c) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']')) {
                valid = false;
                break;
            }
        }
        if (!valid)
            continue;
        out.append("@@||");
        out.append(host);
        out.append("^$elemhide,generichide\n");
    }
    return out;
}

inline std::vector<std::string> parse_host_list(std::string_view csv)
{
    std::vector<std::string> hosts;
    size_t pos = 0;
    while (pos < csv.size()) {
        size_t comma = csv.find(',', pos);
        size_t end = comma == std::string_view::npos ? csv.size() : comma;
        auto token = trim_ascii_whitespace(csv.substr(pos, end - pos));
        pos = comma == std::string_view::npos ? csv.size() : comma + 1;
        if (token.empty())
            continue;
        std::string lower;
        lower.reserve(token.size());
        for (unsigned char c : token)
            lower.push_back(static_cast<char>(std::tolower(c)));
        bool duplicate = false;
        for (auto const& existing : hosts) {
            if (existing == lower) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            hosts.push_back(std::move(lower));
    }
    return hosts;
}

inline std::string serialize_host_list(std::vector<std::string> const& hosts)
{
    std::string out;
    for (size_t i = 0; i < hosts.size(); ++i) {
        if (i > 0)
            out.push_back(',');
        out.append(hosts[i]);
    }
    return out;
}

inline std::string build_bundle_string(
    bool ubo_mode,
    std::vector<ListEntry> const& lists,
    std::string_view custom_filters,
    bool trust_custom_filters,
    std::string_view resources_json = {},
    std::string_view site_switch_rules = {},
    bool logger_enabled = false)
{
    if (!ubo_mode) {
        std::string out;
        for (auto const& list : lists) {
            if (is_ubo_only_list_id(list.id))
                continue;
            out.append(list.contents);
            out.push_back('\n');
        }
        if (!custom_filters.empty()) {
            out.append(custom_filters);
            out.push_back('\n');
        }
        return out;
    }

    if (lists.empty() && custom_filters.empty() && resources_json.empty() && site_switch_rules.empty())
        return {};

    std::string out;
    out.append("!superbird:meta version=1 mode=ubo logger=");
    out.push_back(logger_enabled ? '1' : '0');
    out.push_back('\n');

    for (auto const& list : lists) {
        auto safe_id = sanitize_header_id(list.id);
        out.append("!superbird:list id=");
        out.append(safe_id);
        out.append(" trust=");
        out.push_back(list.trusted ? '1' : '0');
        out.append(" format=");
        out.append(list.is_hosts_format ? "hosts" : "standard");
        out.append(" bytes=");
        out.append(std::to_string(list.contents.size()));
        out.push_back('\n');
        out.append(list.contents);
        out.push_back('\n');
    }

    if (!custom_filters.empty()) {
        out.append("!superbird:list id=custom trust=");
        out.push_back(trust_custom_filters ? '1' : '0');
        out.append(" format=standard bytes=");
        out.append(std::to_string(custom_filters.size()));
        out.push_back('\n');
        out.append(custom_filters);
        out.push_back('\n');
    }

    if (!site_switch_rules.empty()) {
        out.append("!superbird:list id=site-switches trust=0 format=standard bytes=");
        out.append(std::to_string(site_switch_rules.size()));
        out.push_back('\n');
        out.append(site_switch_rules);
        out.push_back('\n');
    }

    if (!resources_json.empty()) {
        out.append("!superbird:resources bytes=");
        out.append(std::to_string(resources_json.size()));
        out.push_back('\n');
        out.append(resources_json);
        out.push_back('\n');
    }

    out.append("!superbird:end\n");
    return out;
}

}
