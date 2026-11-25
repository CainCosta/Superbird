/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Glance/GlanceModel.h>

#include <algorithm>
#include <cmath>

namespace Superbird::Glance {

namespace {

std::string lowercase_prefix(std::string_view text, size_t count)
{
    std::string result(text.substr(0, count));
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    }
    return result;
}

}

Rect sheet_rect(Rect area)
{
    if (area.width <= 0 || area.height <= 0)
        return { area.x, area.y, 0, 0 };

    // ~80% of the extent, at least the minimum, and never past the margins (unless there's no room for margins).
    auto fit = [](int extent, int minimum) {
        auto most = extent - (2 * SHEET_MARGIN);
        if (most <= 0)
            return extent;
        auto scaled = static_cast<int>(std::lround(extent * SHEET_SCALE));
        return std::clamp(scaled, std::min(minimum, most), most);
    };

    auto width = fit(area.width, MINIMUM_SHEET_WIDTH);
    auto height = fit(area.height, MINIMUM_SHEET_HEIGHT);
    return { area.x + ((area.width - width) / 2), area.y + ((area.height - height) / 2), width, height };
}

bool is_glanceable_url(std::string_view url)
{
    auto prefix = lowercase_prefix(url, 8);
    return prefix.starts_with("http://") || prefix.starts_with("https://") || prefix.starts_with("about:");
}

std::string host_of(std::string_view url)
{
    auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos)
        return std::string(url);
    auto rest = url.substr(scheme_end + 3);
    rest = rest.substr(0, rest.find_first_of("/?#"));
    if (auto at = rest.rfind('@'); at != std::string_view::npos)
        rest = rest.substr(at + 1);
    if (rest.substr(0, 4) == "www.")
        rest.remove_prefix(4);
    return std::string(rest);
}

bool GlanceModel::open(std::string_view url, std::optional<OpenerId> opener)
{
    if (!is_glanceable_url(url))
        return false;
    m_state = State::Open;
    m_url = std::string(url);
    m_title.clear();
    m_opener = opener;
    return true;
}

bool GlanceModel::navigated(std::string_view url)
{
    if (m_state != State::Open || url.empty())
        return false;
    if (m_url != url)
        m_title.clear();
    m_url = std::string(url);
    return true;
}

bool GlanceModel::title_changed(std::string_view title)
{
    if (m_state != State::Open)
        return false;
    m_title = std::string(title);
    return true;
}

void GlanceModel::opener_closed(OpenerId opener)
{
    if (m_opener == opener)
        m_opener.reset();
}

std::optional<GlanceModel::Promotion> GlanceModel::promote()
{
    if (m_state != State::Open)
        return {};
    Promotion promotion { m_url, m_opener };
    dismiss();
    return promotion;
}

bool GlanceModel::dismiss()
{
    if (m_state == State::Closed)
        return false;
    m_state = State::Closed;
    m_url.clear();
    m_title.clear();
    m_opener.reset();
    return true;
}

std::string GlanceModel::display_title() const
{
    if (!m_title.empty())
        return m_title;
    return display_host();
}

std::string GlanceModel::display_host() const
{
    return host_of(m_url);
}

}
