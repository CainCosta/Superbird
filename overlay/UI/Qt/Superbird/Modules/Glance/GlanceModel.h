/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Superbird::Glance {

// The tab a Glance was opened from, by an id the caller picks.
using OpenerId = int64_t;

struct Rect {
    int x { 0 };
    int y { 0 };
    int width { 0 };
    int height { 0 };

    bool operator==(Rect const&) const = default;
};

// The sheet covers this share of the page area, within these bounds.
inline constexpr double SHEET_SCALE = 0.8;
inline constexpr int MINIMUM_SHEET_WIDTH = 360;
inline constexpr int MINIMUM_SHEET_HEIGHT = 280;
inline constexpr int SHEET_MARGIN = 16;

// Where the Glance sheet goes in an area (the window's page area): ~80% of it, centered, never smaller than the minimum
// unless the area itself is, and always at least SHEET_MARGIN inside it when there's room.
Rect sheet_rect(Rect area);

// The life of one Glance in a window. Pure, so the transitions are unit-tested on their own:
//
//   Closed --open(url, opener)--> Open --navigated(url)/title_changed--> Open
//   Open --open(url)--> Open (the sheet shows the new link instead)
//   Open --promote()--> Closed, returning what to open as a tab (the page as it is now, under its opener)
//   Open --dismiss()--> Closed
//   opener_closed(opener): the Glance stays, but a promoted tab no longer nests under it.
class GlanceModel {
public:
    enum class State : uint8_t {
        Closed,
        Open,
    };

    State state() const { return m_state; }
    bool is_open() const { return m_state == State::Open; }
    std::string const& url() const { return m_url; }
    std::string const& title() const { return m_title; }
    std::optional<OpenerId> opener() const { return m_opener; }

    // Only http(s) and about: links open in Glance; others are refused (returns false).
    bool open(std::string_view url, std::optional<OpenerId> opener);
    // The page in the sheet went somewhere else (a link inside Glance); it stays in the sheet.
    bool navigated(std::string_view url);
    bool title_changed(std::string_view title);
    void opener_closed(OpenerId);

    struct Promotion {
        std::string url;
        std::optional<OpenerId> opener;
    };
    std::optional<Promotion> promote();
    bool dismiss();

    // The text for the sheet's strip: the title if there is one, else the host of the URL.
    std::string display_title() const;
    std::string display_host() const;

private:
    State m_state { State::Closed };
    std::string m_url;
    std::string m_title;
    std::optional<OpenerId> m_opener;
};

bool is_glanceable_url(std::string_view);
std::string host_of(std::string_view url);

}
