/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Superbird::Workspaces {

// The line icons a workspace can wear instead of a letter or an emoji. They are stored in the workspace's icon text as
// "glyph:<key>", so a workspace keeps the same row format in Workspaces.db and any other icon text still shows as is.
enum class Glyph : uint8_t {
    Home,
    Work,
    School,
    Focus,
    Relax,
    Code,
    Book,
    Heart,
    Star,
    Music,
    Travel,
    Shopping,
    Chat,
    Finance,
    Ideas,
};

struct GlyphInfo {
    Glyph glyph;
    std::string_view key;
    std::string_view label;
};

inline constexpr std::array<GlyphInfo, 15> GLYPHS { {
    { Glyph::Home, "home", "Home" },
    { Glyph::Work, "work", "Work" },
    { Glyph::School, "school", "School" },
    { Glyph::Focus, "focus", "Focus" },
    { Glyph::Relax, "relax", "Relax" },
    { Glyph::Code, "code", "Code" },
    { Glyph::Book, "book", "Reading" },
    { Glyph::Heart, "heart", "Favorites" },
    { Glyph::Star, "star", "Starred" },
    { Glyph::Music, "music", "Music" },
    { Glyph::Travel, "travel", "Travel" },
    { Glyph::Shopping, "shopping", "Shopping" },
    { Glyph::Chat, "chat", "Social" },
    { Glyph::Finance, "finance", "Finance" },
    { Glyph::Ideas, "ideas", "Ideas" },
} };

inline constexpr std::string_view GLYPH_ICON_PREFIX = "glyph:";

GlyphInfo const& glyph_info(Glyph);
// The icon text that stands for a glyph, e.g. "glyph:home".
std::string icon_for_glyph(Glyph);
// The glyph an icon text stands for, if it names one.
std::optional<Glyph> glyph_from_icon(std::string_view icon);

struct NamedTint {
    std::string_view name;
    uint32_t rgb;
};

// The tints offered for workspaces, as 0xRRGGBB.
inline constexpr std::array<NamedTint, 8> TINTS { {
    { "Red", 0xE5484D },
    { "Orange", 0xF76B15 },
    { "Yellow", 0xE2A336 },
    { "Green", 0x30A46C },
    { "Teal", 0x12A594 },
    { "Blue", 0x0090FF },
    { "Purple", 0x8E4EC6 },
    { "Pink", 0xD6409F },
} };

// The tint a new workspace gets when nothing picks one: the next in TINTS after the ones already used.
uint32_t default_tint_for_new_workspace(size_t existing_workspace_count);

// A ready-made workspace the New Workspace picker offers.
struct Preset {
    std::string_view name;
    Glyph glyph;
    uint32_t tint;
};

inline constexpr std::array<Preset, 5> PRESETS { {
    { "Home", Glyph::Home, 0xF76B15 },
    { "Work", Glyph::Work, 0x0090FF },
    { "School", Glyph::School, 0x8E4EC6 },
    { "Focus", Glyph::Focus, 0xE5484D },
    { "Relax", Glyph::Relax, 0x30A46C },
} };

// Whether a preset's name is already taken by one of the given names (ignoring ASCII case and surrounding spaces), so
// the picker doesn't offer a second "Work".
bool preset_is_taken(Preset const&, std::span<std::string const> existing_names);

}
