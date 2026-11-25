/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Superbird::Reader {

// A small, forgiving HTML tree for Reader View. The input is the browser's own serialization of an already-parsed DOM,
// so this only needs to be robust, not spec-complete: it lowercases names, decodes character references, knows void
// and raw-text elements, closes common implied end tags, and never fails. Nothing here executes or fetches anything.
struct Node {
    enum class Type : unsigned char {
        Document,
        Element,
        Text,
        Comment,
    };

    Type type { Type::Document };
    // Lowercased tag name, for elements.
    std::string name;
    // Attribute names are lowercased; values are decoded. Order is kept; duplicates keep the first.
    std::vector<std::pair<std::string, std::string>> attributes;
    // Decoded text, for text and comment nodes.
    std::string text;
    std::vector<std::unique_ptr<Node>> children;
    Node* parent { nullptr };

    bool is_element() const { return type == Type::Element; }
    bool is_element(std::string_view tag) const { return type == Type::Element && name == tag; }
    bool is_text() const { return type == Type::Text; }

    std::optional<std::string_view> attribute(std::string_view attribute_name) const;
    bool has_attribute(std::string_view attribute_name) const { return attribute(attribute_name).has_value(); }

    // The concatenated text of this subtree, without markup, with runs of whitespace collapsed to single spaces.
    std::string text_content() const;
    // Same, but appended to `out` without collapsing, and skipping <script>, <style>, <template> and <noscript>.
    void append_raw_text(std::string& out) const;

    // The first element in this subtree (pre-order, excluding this node) with this tag name.
    Node const* find_first(std::string_view tag) const;
};

// The deepest element nesting kept. Deeper elements become siblings at this depth, so recursive walks stay bounded.
inline constexpr size_t HTML_MAXIMUM_DEPTH = 256;
// Inputs larger than this are cut off (8 MiB).
inline constexpr size_t HTML_MAXIMUM_INPUT_LENGTH = 8 * 1024 * 1024;

std::unique_ptr<Node> parse_html(std::string_view html);

// Decodes HTML character references (`&amp;`, `&#39;`, `&#x1F600;`, a common set of named ones) into UTF-8.
// Unknown references are kept as written; invalid code points become U+FFFD.
std::string decode_html_entities(std::string_view);

std::string escape_html_text(std::string_view);
std::string escape_html_attribute(std::string_view);

bool is_void_element(std::string_view tag);

}
