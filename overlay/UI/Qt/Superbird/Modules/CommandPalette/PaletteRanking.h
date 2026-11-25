/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Superbird::Palette {

enum class EntryKind : uint8_t {
    Calculator,
    Navigate,
    Tab,
    Action,
    Bookmark,
    History,
};

// One row the palette can show. `key` identifies what activating it does: the URL of a tab, bookmark, history entry or
// navigation, or the id of an action on the ModuleRegistry bus.
struct Entry {
    EntryKind kind { EntryKind::Action };
    std::string title;
    std::string subtitle;
    std::string key;
};

struct RankedEntry {
    size_t index { 0 };
    int score { 0 };
    // Byte offsets into the entry's title that matched the query, for highlighting.
    std::vector<size_t> title_positions;
};

enum class QueryMode : uint8_t {
    Search,
    Math,
};

struct ParsedQuery {
    QueryMode mode { QueryMode::Search };
    // The query without surrounding whitespace, and without the leading `=` in math mode.
    std::string_view text;
};

ParsedQuery parse_query(std::string_view input);

std::string_view trim_whitespace(std::string_view);

// Whether the input reads as an address to go to (`example.com`, `https://…`, `about:settings`, `localhost:8080`)
// rather than words to search for.
bool looks_like_url(std::string_view input);

// Drops bookmarks and history entries for pages that are already open in a tab, and history entries for bookmarked
// pages, so each page is offered once, in its most useful form. Order is otherwise kept.
void remove_duplicate_pages(std::vector<Entry>&);
// The same rule, as the ascending indices of the entries to keep, for callers that keep data parallel to the entries.
std::vector<size_t> distinct_page_indices(std::span<Entry const>);

// The extra weight each kind gets on top of its fuzzy score, so that with equal matches an open tab beats an action,
// which beats a bookmark, which beats history.
int kind_weight(EntryKind);

// Ranks entries against a search query, best first, returning at most `limit` of them.
// - An empty query keeps every entry, grouped by kind (tabs, actions, bookmarks, history) in their given order.
// - Otherwise entries must fuzzy-match on their title, or failing that (for less) on their subtitle.
// - A Calculator entry always comes first. A Navigate entry always shows: first when the query looks like an address,
//   last when it would be a search.
std::vector<RankedEntry> rank_entries(std::string_view query, std::span<Entry const> entries, size_t limit);

}
