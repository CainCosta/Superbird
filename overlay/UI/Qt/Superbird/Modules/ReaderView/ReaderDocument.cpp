/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/ReaderView/ArticleExtractor.h>
#include <UI/Qt/Superbird/Modules/ReaderView/HtmlTree.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ReaderDocument.h>
#include <UI/Qt/Superbird/Modules/ReaderView/UrlResolver.h>

#include <algorithm>

namespace Superbird::Reader {

void ReaderPreferences::decrease_font_size()
{
    font_size = std::max(MINIMUM_FONT_SIZE, font_size - FONT_SIZE_STEP);
}

void ReaderPreferences::increase_font_size()
{
    font_size = std::min(MAXIMUM_FONT_SIZE, font_size + FONT_SIZE_STEP);
}

int ReaderPreferences::zoom_by(int delta)
{
    auto before = zoom_steps;
    zoom_steps = std::clamp(zoom_steps + delta, -MAXIMUM_ZOOM_STEPS, MAXIMUM_ZOOM_STEPS);
    return zoom_steps - before;
}

bool ReaderPreferences::makes_same_document_as(ReaderPreferences const& other) const
{
    return font == other.font && font_size == other.font_size && line_height == other.line_height && width == other.width && theme == other.theme;
}

void ReaderPreferences::cycle_line_height()
{
    line_height += LINE_HEIGHT_STEP;
    if (line_height > MAXIMUM_LINE_HEIGHT)
        line_height = MINIMUM_LINE_HEIGHT;
}

namespace {

struct Palette {
    char const* background;
    char const* text;
    char const* muted;
    char const* link;
    char const* rule;
    char const* code_background;
};

constexpr Palette LIGHT { "#ffffff", "#1d1d1f", "#6e6e73", "#0a64d6", "#e3e3e6", "#f3f3f5" };
constexpr Palette SEPIA { "#f4ecd8", "#4b3a26", "#7d6a52", "#8a4b0f", "#e2d6bb", "#ebe0c6" };
constexpr Palette DARK { "#1c1c1e", "#e6e6e8", "#a1a1a6", "#6cb4ff", "#3a3a3c", "#2c2c2e" };

std::string palette_css(Palette const& palette)
{
    return std::string("--background:") + palette.background + ";--text:" + palette.text + ";--muted:" + palette.muted
        + ";--link:" + palette.link + ";--rule:" + palette.rule + ";--code:" + palette.code_background + ";";
}

char const* font_stack(ReaderFont font)
{
    switch (font) {
    case ReaderFont::Serif:
        return "Charter, 'Iowan Old Style', 'Source Serif Pro', Georgia, 'DejaVu Serif', serif";
    case ReaderFont::Sans:
        return "system-ui, -apple-system, 'Inter', 'Helvetica Neue', Arial, 'DejaVu Sans', sans-serif";
    case ReaderFont::Mono:
        return "ui-monospace, 'SF Mono', Menlo, 'DejaVu Sans Mono', monospace";
    }
    return "serif";
}

char const* column_width(ReaderWidth width)
{
    switch (width) {
    case ReaderWidth::Narrow:
        return "32em";
    case ReaderWidth::Medium:
        return "40em";
    case ReaderWidth::Wide:
        return "52em";
    }
    return "40em";
}

std::string theme_css(ReaderTheme theme)
{
    switch (theme) {
    case ReaderTheme::Light:
        return ":root{color-scheme:light;" + palette_css(LIGHT) + "}";
    case ReaderTheme::Sepia:
        return ":root{color-scheme:light;" + palette_css(SEPIA) + "}";
    case ReaderTheme::Dark:
        return ":root{color-scheme:dark;" + palette_css(DARK) + "}";
    case ReaderTheme::System:
        return ":root{color-scheme:light dark;" + palette_css(LIGHT) + "}@media (prefers-color-scheme: dark){:root{" + palette_css(DARK) + "}}";
    }
    return {};
}

}

std::string build_reader_document(Article const& article, std::string_view page_url, ReaderPreferences const& preferences)
{
    auto line_height = std::to_string(preferences.line_height / 10) + "." + std::to_string(preferences.line_height % 10);

    std::string html;
    html.reserve(article.content_html.size() + 4096);
    html += "<!DOCTYPE html><html";
    if (!article.language.empty())
        html += " lang=\"" + escape_html_attribute(article.language) + "\"";
    if (!article.direction.empty())
        html += " dir=\"" + escape_html_attribute(article.direction) + "\"";
    html += "><head><meta charset=\"utf-8\">";
    // Defense in depth on top of the sanitizer and the view's disabled scripting.
    html += "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; img-src http: https:; style-src 'unsafe-inline'; "
            "script-src 'none'; object-src 'none'; frame-src 'none'; form-action 'none'; base-uri 'none'\">";
    html += "<meta name=\"referrer\" content=\"no-referrer\">";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<title>" + escape_html_text(article.title) + "</title><style>";
    html += theme_css(preferences.theme);
    html += "html{background:var(--background);}";
    html += "body{margin:0;padding:3.5em 1.5em 5em;background:var(--background);color:var(--text);font-family:";
    html += font_stack(preferences.font);
    html += ";font-size:" + std::to_string(preferences.font_size) + "px;line-height:" + line_height + ";";
    html += "-webkit-font-smoothing:antialiased;text-rendering:optimizeLegibility;overflow-wrap:break-word;}";
    html += "main{max-width:";
    html += column_width(preferences.width);
    html += ";margin:0 auto;}";
    html += "header{margin-bottom:2em;padding-bottom:1.25em;border-bottom:1px solid var(--rule);}";
    html += ".site{font-family:system-ui,-apple-system,sans-serif;font-size:.75em;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);margin:0 0 .6em;}";
    html += "h1{font-size:1.9em;line-height:1.2;margin:0 0 .45em;font-weight:700;}";
    html += ".meta{font-family:system-ui,-apple-system,sans-serif;font-size:.8em;color:var(--muted);margin:0;}";
    html += ".meta a{color:var(--muted);}";
    html += "article h2{font-size:1.4em;line-height:1.25;margin:1.6em 0 .5em;}";
    html += "article h3{font-size:1.2em;margin:1.4em 0 .4em;}article h4,article h5,article h6{font-size:1em;margin:1.2em 0 .3em;}";
    html += "article p,article ul,article ol,article dl,article blockquote,article pre,article figure,article table{margin:0 0 1em;}";
    html += "a{color:var(--link);text-decoration:underline;text-decoration-thickness:1px;text-underline-offset:.15em;}";
    html += "img{max-width:100%;height:auto;display:block;margin:1.2em auto;border-radius:4px;}";
    html += "figure{margin-left:0;margin-right:0;}figcaption{font-size:.8em;color:var(--muted);text-align:center;margin-top:-.6em;}";
    html += "blockquote{margin-left:0;padding-left:1em;border-left:3px solid var(--rule);color:var(--muted);font-style:italic;}";
    html += "pre,code,kbd,samp{font-family:ui-monospace,'SF Mono',Menlo,monospace;font-size:.85em;}";
    html += "pre{background:var(--code);padding:.8em 1em;border-radius:6px;overflow-x:auto;line-height:1.45;}";
    html += "code{background:var(--code);padding:.1em .3em;border-radius:4px;}pre code{background:none;padding:0;}";
    html += "table{border-collapse:collapse;width:100%;font-size:.9em;}th,td{border:1px solid var(--rule);padding:.35em .6em;text-align:left;vertical-align:top;}";
    html += "hr{border:none;border-top:1px solid var(--rule);margin:2em 0;}";
    html += "</style></head><body><main><header>";

    if (!article.site_name.empty())
        html += "<p class=\"site\">" + escape_html_text(article.site_name) + "</p>";
    html += "<h1>" + escape_html_text(article.title.empty() ? std::string("Untitled") : article.title) + "</h1>";

    html += "<p class=\"meta\">";
    bool has_meta = false;
    if (!article.byline.empty()) {
        html += escape_html_text(article.byline);
        has_meta = true;
    }
    if (article.word_count > 0) {
        if (has_meta)
            html += " \xC2\xB7 ";
        html += std::to_string(article.reading_minutes) + " min read";
        has_meta = true;
    }
    if (auto original = safe_link_url(page_url, page_url); original.has_value()) {
        if (has_meta)
            html += " \xC2\xB7 ";
        html += "<a href=\"" + escape_html_attribute(*original) + "\">View original</a>";
    }
    html += "</p></header><article>";
    html += article.content_html;
    html += "</article></main></body></html>";
    return html;
}

}
