/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Superbird::Reader {

struct Article;

enum class ReaderFont : uint8_t {
    Serif,
    Sans,
    Mono,
};

enum class ReaderTheme : uint8_t {
    System,
    Light,
    Sepia,
    Dark,
};

enum class ReaderWidth : uint8_t {
    Narrow,
    Medium,
    Wide,
};

struct ReaderPreferences {
    static constexpr int MINIMUM_FONT_SIZE = 14;
    static constexpr int MAXIMUM_FONT_SIZE = 32;
    static constexpr int FONT_SIZE_STEP = 2;
    // Line height in tenths (16 = 1.6), so preferences compare exactly.
    static constexpr int MINIMUM_LINE_HEIGHT = 12;
    static constexpr int MAXIMUM_LINE_HEIGHT = 22;
    static constexpr int LINE_HEIGHT_STEP = 2;

    ReaderFont font { ReaderFont::Serif };
    int font_size { 20 };
    int line_height { 16 };
    ReaderWidth width { ReaderWidth::Medium };
    ReaderTheme theme { ReaderTheme::System };

    bool can_decrease_font_size() const { return font_size > MINIMUM_FONT_SIZE; }
    bool can_increase_font_size() const { return font_size < MAXIMUM_FONT_SIZE; }
    void decrease_font_size();
    void increase_font_size();
    void cycle_line_height();

    bool operator==(ReaderPreferences const&) const = default;
};

// The full, self-contained reader page for an article: a strict Content-Security-Policy (no scripts, no frames, no
// forms, no plugins, images from http(s) only), the reader stylesheet for the preferences (System theme follows
// prefers-color-scheme), a header with the site name, title, byline, reading time and a link to the original page, and
// the article's already-sanitized content. Every piece of metadata is escaped.
std::string build_reader_document(Article const&, std::string_view page_url, ReaderPreferences const&);

}
