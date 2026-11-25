/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacePresets.h>

#include <algorithm>

namespace Superbird::Workspaces {

GlyphInfo const& glyph_info(Glyph glyph)
{
    for (auto const& info : GLYPHS) {
        if (info.glyph == glyph)
            return info;
    }
    return GLYPHS[0];
}

std::string icon_for_glyph(Glyph glyph)
{
    std::string icon(GLYPH_ICON_PREFIX);
    icon += glyph_info(glyph).key;
    return icon;
}

std::optional<Glyph> glyph_from_icon(std::string_view icon)
{
    if (!icon.starts_with(GLYPH_ICON_PREFIX))
        return {};
    auto key = icon.substr(GLYPH_ICON_PREFIX.size());
    for (auto const& info : GLYPHS) {
        if (info.key == key)
            return info.glyph;
    }
    return {};
}

uint32_t default_tint_for_new_workspace(size_t existing_workspace_count)
{
    return TINTS[existing_workspace_count % TINTS.size()].rgb;
}

static std::string_view trimmed(std::string_view text)
{
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!text.empty() && is_space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))
        text.remove_suffix(1);
    return text;
}

static bool equals_ignoring_ascii_case(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, [](char x, char y) {
        auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        return lower(x) == lower(y);
    });
}

bool preset_is_taken(Preset const& preset, std::span<std::string const> existing_names)
{
    return std::ranges::any_of(existing_names, [&](std::string const& name) {
        return equals_ignoring_ascii_case(trimmed(name), preset.name);
    });
}

}
