/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/CommandPalette/FuzzyMatcher.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/PaletteRanking.h>

#include <algorithm>
#include <climits>
#include <unordered_set>

namespace Superbird::Palette {

namespace {

constexpr bool is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

constexpr bool is_ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }

bool starts_with_ignoring_case(std::string_view haystack, std::string_view prefix)
{
    if (haystack.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        auto a = haystack[i];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a + ('a' - 'A'));
        if (a != prefix[i])
            return false;
    }
    return true;
}

int kind_order(EntryKind kind)
{
    switch (kind) {
    case EntryKind::Calculator:
        return 0;
    case EntryKind::Navigate:
        return 1;
    case EntryKind::Tab:
        return 2;
    case EntryKind::Action:
        return 3;
    case EntryKind::Bookmark:
        return 4;
    case EntryKind::History:
        return 5;
    }
    return 6;
}

std::string page_identity(std::string_view url)
{
    auto end = url.find('#');
    auto identity = std::string(url.substr(0, end));
    while (identity.size() > 1 && identity.back() == '/')
        identity.pop_back();
    return identity;
}

bool is_page_kind(EntryKind kind)
{
    return kind == EntryKind::Tab || kind == EntryKind::Bookmark || kind == EntryKind::History;
}

}

std::string_view trim_whitespace(std::string_view input)
{
    while (!input.empty() && is_whitespace(input.front()))
        input.remove_prefix(1);
    while (!input.empty() && is_whitespace(input.back()))
        input.remove_suffix(1);
    return input;
}

ParsedQuery parse_query(std::string_view input)
{
    auto text = trim_whitespace(input);
    if (!text.empty() && text.front() == '=')
        return { QueryMode::Math, trim_whitespace(text.substr(1)) };
    return { QueryMode::Search, text };
}

bool looks_like_url(std::string_view input)
{
    auto text = trim_whitespace(input);
    if (text.empty())
        return false;
    for (auto c : text) {
        if (is_whitespace(c))
            return false;
    }

    if (text.find("://") != std::string_view::npos)
        return true;
    for (auto scheme : { "about:", "file:", "data:", "view-source:", "mailto:" }) {
        if (starts_with_ignoring_case(text, scheme))
            return text.size() > std::string_view(scheme).size();
    }

    auto host_end = text.find_first_of("/?#");
    auto host = text.substr(0, host_end);
    if (auto colon = host.find(':'); colon != std::string_view::npos) {
        auto port = host.substr(colon + 1);
        if (port.empty() || !std::all_of(port.begin(), port.end(), is_ascii_digit))
            return false;
        host = host.substr(0, colon);
    }
    if (host.empty())
        return false;
    if (starts_with_ignoring_case(host, "localhost") && host.size() == 9)
        return true;

    if (host.front() == '.' || host.back() == '.' || host.find("..") != std::string_view::npos)
        return false;
    for (auto c : host) {
        if (!is_ascii_alpha(c) && !is_ascii_digit(c) && c != '-' && c != '.')
            return false;
    }

    auto last_dot = host.rfind('.');
    if (last_dot == std::string_view::npos)
        return false;

    auto top_level = host.substr(last_dot + 1);
    if (top_level.size() >= 2 && std::all_of(top_level.begin(), top_level.end(), is_ascii_alpha))
        return true;

    // A dotted IPv4 address.
    size_t parts = 0;
    size_t start = 0;
    while (start <= host.size()) {
        auto end = host.find('.', start);
        if (end == std::string_view::npos)
            end = host.size();
        auto part = host.substr(start, end - start);
        if (part.empty() || part.size() > 3 || !std::all_of(part.begin(), part.end(), is_ascii_digit))
            return false;
        ++parts;
        start = end + 1;
    }
    return parts == 4;
}

std::vector<size_t> distinct_page_indices(std::span<Entry const> entries)
{
    std::unordered_set<std::string> open_tabs;
    std::unordered_set<std::string> bookmarks;
    for (auto const& entry : entries) {
        if (entry.key.empty())
            continue;
        if (entry.kind == EntryKind::Tab)
            open_tabs.insert(page_identity(entry.key));
        else if (entry.kind == EntryKind::Bookmark)
            bookmarks.insert(page_identity(entry.key));
    }

    std::vector<size_t> kept;
    kept.reserve(entries.size());
    std::unordered_set<std::string> seen_bookmarks;
    std::unordered_set<std::string> seen_history;
    for (size_t index = 0; index < entries.size(); ++index) {
        auto const& entry = entries[index];
        if (entry.key.empty() || !is_page_kind(entry.kind) || entry.kind == EntryKind::Tab) {
            kept.push_back(index);
            continue;
        }
        auto identity = page_identity(entry.key);
        if (open_tabs.contains(identity))
            continue;
        if (entry.kind == EntryKind::Bookmark) {
            if (seen_bookmarks.insert(identity).second)
                kept.push_back(index);
            continue;
        }
        if (bookmarks.contains(identity))
            continue;
        if (seen_history.insert(identity).second)
            kept.push_back(index);
    }
    return kept;
}

void remove_duplicate_pages(std::vector<Entry>& entries)
{
    auto kept = distinct_page_indices(entries);
    std::vector<Entry> result;
    result.reserve(kept.size());
    for (auto index : kept)
        result.push_back(std::move(entries[index]));
    entries = std::move(result);
}

int kind_weight(EntryKind kind)
{
    switch (kind) {
    case EntryKind::Tab:
        return 24;
    case EntryKind::Action:
        return 18;
    case EntryKind::Bookmark:
        return 10;
    case EntryKind::History:
    case EntryKind::Calculator:
    case EntryKind::Navigate:
        return 0;
    }
    return 0;
}

std::vector<RankedEntry> rank_entries(std::string_view query, std::span<Entry const> entries, size_t limit)
{
    auto text = trim_whitespace(query);
    auto query_is_address = looks_like_url(text);

    std::vector<RankedEntry> ranked;
    ranked.reserve(entries.size());

    for (size_t index = 0; index < entries.size(); ++index) {
        auto const& entry = entries[index];

        if (entry.kind == EntryKind::Calculator) {
            ranked.push_back({ index, INT_MAX, {} });
            continue;
        }
        if (entry.kind == EntryKind::Navigate) {
            ranked.push_back({ index, query_is_address ? INT_MAX - 1 : INT_MIN + 1, {} });
            continue;
        }
        if (text.empty()) {
            ranked.push_back({ index, 0, {} });
            continue;
        }

        auto title_match = fuzzy_match(text, entry.title);
        auto subtitle_match = entry.subtitle.empty() ? std::nullopt : fuzzy_match(text, entry.subtitle);
        if (!title_match.has_value() && !subtitle_match.has_value())
            continue;

        int score = INT_MIN;
        std::vector<size_t> positions;
        if (title_match.has_value()) {
            score = title_match->score;
            positions = std::move(title_match->positions);
        }
        if (subtitle_match.has_value()) {
            auto subtitle_score = subtitle_match->score / 2 - 8;
            if (subtitle_score > score) {
                score = subtitle_score;
                positions.clear();
            }
        }
        ranked.push_back({ index, score + kind_weight(entry.kind), std::move(positions) });
    }

    std::stable_sort(ranked.begin(), ranked.end(), [&](RankedEntry const& a, RankedEntry const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        auto const& entry_a = entries[a.index];
        auto const& entry_b = entries[b.index];
        if (kind_order(entry_a.kind) != kind_order(entry_b.kind))
            return kind_order(entry_a.kind) < kind_order(entry_b.kind);
        // In an empty query, the given order within a kind is what counts (e.g. most recently used tabs first).
        if (!text.empty() && entry_a.title.size() != entry_b.title.size())
            return entry_a.title.size() < entry_b.title.size();
        return a.index < b.index;
    });

    if (ranked.size() > limit) {
        // Keep a trailing Navigate entry even when the list is cut short, so a search is always one key away.
        auto navigate = std::find_if(ranked.begin(), ranked.end(), [&](auto const& r) {
            return entries[r.index].kind == EntryKind::Navigate;
        });
        if (navigate != ranked.end() && static_cast<size_t>(navigate - ranked.begin()) >= limit && limit > 0) {
            auto kept = *navigate;
            ranked.resize(limit - 1);
            ranked.push_back(std::move(kept));
        } else {
            ranked.resize(limit);
        }
    }
    return ranked;
}

}
