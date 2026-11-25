/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Superbird::Reader {

// Resolves `reference` against the absolute `base` URL following RFC 3986 section 5 (with the usual browser leniency:
// ASCII tab/newline characters are dropped and surrounding whitespace is trimmed first, and backslashes count as
// slashes in http(s) URLs). Returns nothing if `base` is not absolute and `reference` isn't either.
std::optional<std::string> resolve_url(std::string_view base, std::string_view reference);

// The lowercased scheme of an absolute URL (after the same tab/newline/whitespace cleanup), if it has one.
std::optional<std::string> url_scheme(std::string_view url);

// Where Reader View lets a link go: http, https and mailto URLs, or a fragment on the same document. Everything else,
// notably javascript:, data:, vbscript: and file:, is refused however it is spelled or encoded.
std::optional<std::string> safe_link_url(std::string_view base, std::string_view reference);

// Where Reader View lets an image load from: http and https only.
std::optional<std::string> safe_image_url(std::string_view base, std::string_view reference);

}
