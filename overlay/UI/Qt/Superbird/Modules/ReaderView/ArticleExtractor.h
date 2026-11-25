/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Superbird::Reader {

struct Node;

struct Article {
    // Whether the page reads as an article at all. When false, Reader View offers no icon (the user may still force
    // it from the Command Palette, and gets whatever text was found).
    bool is_article { false };

    std::string title;
    std::string byline;
    std::string site_name;
    std::string excerpt;
    std::string language;
    // "ltr", "rtl", or empty.
    std::string direction;

    // The article body as sanitized HTML: an allow-list of text-level and structural elements only; no scripts,
    // styles, frames, forms, embeds, event handlers, classes, ids or inline styles; links only to http(s)/mailto or
    // in-page fragments; images only from http(s). Relative URLs are resolved against the page's base URL.
    std::string content_html;
    // The article's plain text length in bytes, and its word count.
    size_t text_length { 0 };
    size_t word_count { 0 };
    int reading_minutes { 0 };
};

// Extracts the main article from a serialized document. `page_url` is the document's URL; a <base href> in the
// document refines it. Never fails: a page without an article yields `is_article == false`.
Article extract_article(std::string_view html, std::string_view page_url);

// Firefox's "is probably readerable" heuristic: enough paragraphs of real prose outside unlikely regions. Cheap
// enough to run on every page load.
bool is_probably_readerable(std::string_view html);
bool is_probably_readerable(Node const& document);

// The best image candidate in a srcset attribute: the widest (`640w`) or densest (`2x`) one, else the first. Returns
// nothing for an empty or unparsable srcset.
std::optional<std::string> best_srcset_candidate(std::string_view srcset);

// Words in text: runs of non-whitespace, with each CJK ideograph or kana counted as its own word.
size_t count_words(std::string_view text);

// Minutes to read this many words at an average adult silent reading speed (238 wpm), rounded up, at least 1.
int reading_time_minutes(size_t words);

}
