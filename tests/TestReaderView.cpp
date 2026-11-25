/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/ReaderView/ArticleExtractor.h>
#include <UI/Qt/Superbird/Modules/ReaderView/HtmlTree.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ReaderDocument.h>
#include <UI/Qt/Superbird/Modules/ReaderView/UrlResolver.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace Superbird::Reader;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                          \
        }                                                                          \
    } while (0)

static bool has(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

static std::string lower(std::string_view text)
{
    std::string out(text);
    for (auto& c : out) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
    return out;
}

static std::string paragraph(int index)
{
    return "<p>This is paragraph number " + std::to_string(index)
        + " of a long and thoughtful article, which goes on at some length, with commas, clauses, and the kind of prose that "
          "a reader would actually want to sit down and read without the noise of the surrounding page.</p>";
}

static std::string article_page(std::string extra_body = {}, std::string extra_article = {})
{
    std::string html = "<!DOCTYPE html><html lang=\"en\"><head><title>The Long Read | Example News</title>"
                       "<meta property=\"og:site_name\" content=\"Example News\">"
                       "<meta name=\"author\" content=\"Ada Lovelace\">"
                       "<meta name=\"description\" content=\"A summary.\"></head><body>"
                       "<nav class=\"site-nav\"><a href=\"/\">Home</a><a href=\"/world\">World</a><a href=\"/tech\">Tech</a></nav>"
                       "<div class=\"sidebar\"><p>Trending: a short list of other stories you might like, with plenty of links.</p></div>"
                       "<div id=\"main-content\" class=\"article-body\"><h1>The Long Read</h1>";
    for (int i = 0; i < 8; ++i)
        html += paragraph(i);
    html += extra_article;
    html += "</div><footer><p>Copyright Example News. All rights reserved. Terms, privacy, cookies and more links.</p></footer>";
    html += extra_body;
    html += "</body></html>";
    return html;
}

// ---- HTML tree ----

static void test_parser_basics()
{
    auto document = parse_html("<P CLASS=a>Hello <b>wor&amp;ld</b><br/>x &lt;y&gt; &#65;&#x42;&nbsp;&unknown;</p>");
    auto const* p = document->find_first("p");
    EXPECT(p);
    EXPECT(p->attribute("class").value_or("") == "a");
    EXPECT(p->text_content() == "Hello wor&ldx <y> AB\xC2\xA0&unknown;");
    EXPECT(document->find_first("br"));
}

static void test_parser_implied_end_tags_and_raw_text()
{
    auto document = parse_html("<ul><li>one<li>two</ul><p>a<p>b<div>c</div><script>if (a < b && c) { x = '</p>'; }</script>");
    auto const* ul = document->find_first("ul");
    EXPECT(ul && ul->children.size() == 2);
    auto const* script = document->find_first("script");
    EXPECT(script && script->children.size() == 1 && has(script->children[0]->text, "x = '</p>'"));
    // Scripts don't count as text.
    EXPECT(!has(document->text_content(), "x = "));
}

static void test_parser_survives_hostile_input()
{
    std::string deep;
    for (int i = 0; i < 5000; ++i)
        deep += "<div>";
    deep += "bottom";
    auto document = parse_html(deep);
    EXPECT(has(document->text_content(), "bottom"));

    for (auto input : { "<", "<a", "<a href=", "<a href=\"", "</", "<!--", "<!", "&", "&#", "&#x", "&#99999999999;", "<script>", "<p a=1 a=2 b>" }) {
        auto parsed = parse_html(input);
        EXPECT(parsed != nullptr);
    }
    EXPECT(decode_html_entities("&#99999999999;") == "\xEF\xBF\xBD");
    EXPECT(decode_html_entities("&#0;") == "\xEF\xBF\xBD");
    EXPECT(decode_html_entities("a &amp b") == "a & b");
}

// ---- URLs ----

static void test_url_resolution()
{
    std::string base = "http://a/b/c/d;p?q";
    // RFC 3986 5.4.1 / 5.4.2 examples.
    EXPECT(resolve_url(base, "g").value() == "http://a/b/c/g");
    EXPECT(resolve_url(base, "./g").value() == "http://a/b/c/g");
    EXPECT(resolve_url(base, "g/").value() == "http://a/b/c/g/");
    EXPECT(resolve_url(base, "/g").value() == "http://a/g");
    EXPECT(resolve_url(base, "//g").value() == "http://g/");
    EXPECT(resolve_url(base, "?y").value() == "http://a/b/c/d;p?y");
    EXPECT(resolve_url(base, "#s").value() == "http://a/b/c/d;p?q#s");
    EXPECT(resolve_url(base, "").value() == "http://a/b/c/d;p?q");
    EXPECT(resolve_url(base, ".").value() == "http://a/b/c/");
    EXPECT(resolve_url(base, "..").value() == "http://a/b/");
    EXPECT(resolve_url(base, "../g").value() == "http://a/b/g");
    EXPECT(resolve_url(base, "../../g").value() == "http://a/g");
    EXPECT(resolve_url(base, "../../../g").value() == "http://a/g");
    EXPECT(resolve_url(base, "/./g").value() == "http://a/g");
    EXPECT(resolve_url(base, "g;x=1/../y").value() == "http://a/b/c/y");
    EXPECT(resolve_url(base, "https://other.example/x").value() == "https://other.example/x");
    EXPECT(resolve_url("https://example.com", "img.png").value() == "https://example.com/img.png");
    EXPECT(resolve_url("https://example.com/a/b", "\\\\evil.example\\x").value() == "https://evil.example/x");
    EXPECT(resolve_url("https://example.com/a/", " \t img.png\n").value() == "https://example.com/a/img.png");
    EXPECT(!resolve_url("not a url", "g").has_value());
}

static void test_url_scheme_filtering()
{
    std::string base = "https://example.com/post/";
    for (auto evil : { "javascript:alert(1)", "JaVaScRiPt:alert(1)", " javascript:alert(1)", "java\tscript:alert(1)",
             "java\nscript:alert(1)", "\x01javascript:alert(1)", "vbscript:msgbox(1)", "data:text/html,<script>alert(1)</script>",
             "file:///etc/passwd", "about:blank" }) {
        EXPECT(!safe_link_url(base, evil).has_value());
        EXPECT(!safe_image_url(base, evil).has_value());
    }
    EXPECT(safe_link_url(base, "/about").value() == "https://example.com/about");
    EXPECT(safe_link_url(base, "mailto:a@example.com").value() == "mailto:a@example.com");
    EXPECT(safe_link_url(base, "#section").value() == "#section");
    EXPECT(!safe_image_url(base, "mailto:a@example.com").has_value());
    EXPECT(safe_image_url(base, "pic.jpg").value() == "https://example.com/post/pic.jpg");
}

// ---- Extraction ----

static void test_extracts_article_and_metadata()
{
    auto article = extract_article(article_page(), "https://www.example.com/2026/long-read");
    EXPECT(article.is_article);
    EXPECT(article.title == "The Long Read");
    EXPECT(article.byline == "Ada Lovelace");
    EXPECT(article.site_name == "Example News");
    EXPECT(article.excerpt == "A summary.");
    EXPECT(article.language == "en");
    EXPECT(has(article.content_html, "paragraph number 0"));
    EXPECT(has(article.content_html, "paragraph number 7"));
    // Navigation, sidebar and footer noise is gone.
    EXPECT(!has(article.content_html, "Trending"));
    EXPECT(!has(article.content_html, "Copyright"));
    EXPECT(!has(article.content_html, ">Home<"));
    // The heading repeating the title is dropped (the reader header shows it).
    EXPECT(!has(article.content_html, ">The Long Read<"));
}

static void test_title_and_site_fallbacks()
{
    auto page = article_page();
    auto replace = [&](std::string const& from, std::string const& to) {
        auto at = page.find(from);
        if (at != std::string::npos)
            page.replace(at, from.size(), to);
    };
    replace("<meta property=\"og:site_name\" content=\"Example News\">", "");
    replace("<title>The Long Read | Example News</title>", "<title>Short - Site</title>");
    auto article = extract_article(page, "https://www.example.com/x");
    EXPECT(article.site_name == "example.com");
    // A head segment with fewer than three words keeps the full title.
    EXPECT(article.title == "Short - Site");
}

static void test_byline_from_markup()
{
    std::string page = article_page({}, "<p class=\"byline\">By Grace Hopper</p>");
    auto at = page.find("<meta name=\"author\" content=\"Ada Lovelace\">");
    page.erase(at, std::string("<meta name=\"author\" content=\"Ada Lovelace\">").size());
    auto article = extract_article(page, "https://example.com/");
    EXPECT(article.byline == "Grace Hopper");
}

static void test_scoring_prefers_dense_prose_over_link_lists()
{
    std::string html = "<html><body><div class=\"links\">";
    for (int i = 0; i < 30; ++i)
        html += "<p><a href=\"/l" + std::to_string(i) + "\">A link that has fairly long anchor text, number " + std::to_string(i) + "</a></p>";
    html += "</div><div class=\"story\">";
    for (int i = 0; i < 6; ++i)
        html += paragraph(i);
    html += "</div></body></html>";
    auto article = extract_article(html, "https://example.com/");
    EXPECT(has(article.content_html, "paragraph number 3"));
    EXPECT(!has(article.content_html, "fairly long anchor text"));
}

static void test_relative_urls_resolve_against_base()
{
    auto article = extract_article(article_page({}, "<p>See <a href=\"../more\">more</a> and <img src=\"pics/a.jpg\" alt=\"A\" width=\"640\" height=\"x\"> "
                                                    "plus <img src=\"data:image/gif;base64,R0lGOD\" data-src=\"/lazy.jpg\">.</p>"),
        "https://example.com/news/2026/story");
    EXPECT(has(article.content_html, "href=\"https://example.com/news/more\""));
    EXPECT(has(article.content_html, "src=\"https://example.com/news/2026/pics/a.jpg\""));
    EXPECT(has(article.content_html, "width=\"640\""));
    EXPECT(!has(article.content_html, "height=\"x\""));
    EXPECT(has(article.content_html, "src=\"https://example.com/lazy.jpg\""));
    EXPECT(!has(article.content_html, "data:image"));

    auto with_base = article_page({}, "<p><a href=\"page\">rel</a></p>");
    with_base.insert(with_base.find("</head>"), "<base href=\"https://cdn.example.org/root/\">");
    auto based = extract_article(with_base, "https://example.com/a/b");
    EXPECT(has(based.content_html, "href=\"https://cdn.example.org/root/page\""));

    // A javascript: base is ignored.
    auto evil_base = article_page({}, "<p><a href=\"page\">rel</a></p>");
    evil_base.insert(evil_base.find("</head>"), "<base href=\"javascript:alert(1)//\">");
    auto ignored = extract_article(evil_base, "https://example.com/a/b");
    EXPECT(has(ignored.content_html, "href=\"https://example.com/a/page\""));
}

static void test_sanitizes_scripts_frames_forms_and_handlers()
{
    std::string hostile = "<script>alert('script')</script>"
                          "<p onclick=\"alert('onclick')\" onmouseover=alert(1) style=\"color:red\" class=\"x\" id=\"y\">Clickable text in a paragraph that is long enough.</p>"
                          "<iframe src=\"https://evil.example/frame\"></iframe>"
                          "<form action=\"https://evil.example/steal\"><input name=\"password\"><button formaction=\"javascript:alert(1)\">Go</button><p>Form text stays readable.</p></form>"
                          "<img src=\"x.png\" onerror=\"alert('onerror')\" srcset=\"javascript:alert(1) 1x\">"
                          "<a href=\"javascript:alert('link')\">bad link</a>"
                          "<a href=\"&#106;avascript:alert('entity')\">entity link</a>"
                          "<a href=\"java&#x09;script:alert('tab')\">tab link</a>"
                          "<a href=\" JAVASCRIPT:alert('caps')\">caps link</a>"
                          "<a href=\"data:text/html,&lt;script&gt;alert(1)&lt;/script&gt;\">data link</a>"
                          "<object data=\"x.swf\"></object><embed src=\"x.swf\">"
                          "<svg onload=\"alert('svg')\"><script>alert(2)</script></svg>"
                          "<math><mi xlink:href=\"javascript:alert(1)\">x</mi></math>"
                          "<style>body{background:url(javascript:alert(1))}</style>"
                          "<meta http-equiv=\"refresh\" content=\"0;url=javascript:alert(1)\">"
                          "<base href=\"javascript:alert(1)//\">"
                          "<template><img src=x onerror=alert(1)></template>"
                          "<noscript><img src=x onerror=alert(1)></noscript>"
                          "<details open ontoggle=\"alert('toggle')\"><summary>More</summary>Details body.</details>"
                          "<custom-element onfocus=\"alert(1)\">Custom content.</custom-element>"
                          "<p>&lt;script&gt;alert('escaped')&lt;/script&gt; stays as text</p>";
    auto article = extract_article(article_page({}, hostile), "https://example.com/post");
    auto out = lower(article.content_html);

    EXPECT(!has(out, "<script"));
    EXPECT(!has(out, "<iframe"));
    EXPECT(!has(out, "<form"));
    EXPECT(!has(out, "<input"));
    EXPECT(!has(out, "<button"));
    EXPECT(!has(out, "<object"));
    EXPECT(!has(out, "<embed"));
    EXPECT(!has(out, "<svg"));
    EXPECT(!has(out, "<math"));
    EXPECT(!has(out, "<style"));
    EXPECT(!has(out, "<meta"));
    EXPECT(!has(out, "<base"));
    EXPECT(!has(out, "<template"));
    EXPECT(!has(out, "<noscript"));
    EXPECT(!has(out, "<custom-element"));
    EXPECT(!has(out, "javascript:"));
    EXPECT(!has(out, "data:text"));
    EXPECT(!has(out, "onclick"));
    EXPECT(!has(out, "onmouseover"));
    EXPECT(!has(out, "onerror"));
    EXPECT(!has(out, "onload"));
    EXPECT(!has(out, "ontoggle"));
    EXPECT(!has(out, "onfocus"));
    EXPECT(!has(out, "srcset"));
    EXPECT(!has(out, "style="));
    EXPECT(!has(out, "class="));
    EXPECT(!has(out, " id="));
    EXPECT(!has(out, "formaction"));

    // Text survives; unsafe links keep their words but lose their targets.
    EXPECT(has(out, "clickable text in a paragraph"));
    EXPECT(has(out, "form text stays readable"));
    EXPECT(has(out, "bad link"));
    EXPECT(has(out, "entity link"));
    EXPECT(has(out, "custom content."));
    EXPECT(has(out, "&lt;script&gt;alert('escaped')&lt;/script&gt; stays as text"));
    EXPECT(has(out, "<img src=\"https://example.com/x.png\""));
}

static void test_hidden_content_is_dropped()
{
    auto article = extract_article(article_page({}, "<p hidden>Hidden one.</p><p aria-hidden=\"true\">Hidden two.</p><p style=\"display: none\">Hidden three.</p><div role=\"navigation\">Nav role.</div>"),
        "https://example.com/");
    EXPECT(!has(article.content_html, "Hidden"));
    EXPECT(!has(article.content_html, "Nav role"));
}

static void test_non_article_detection()
{
    // A link hub / app shell: little prose.
    std::string hub = "<html><body><nav>Menu</nav><ul>";
    for (int i = 0; i < 40; ++i)
        hub += "<li><a href=\"/item" + std::to_string(i) + "\">Item " + std::to_string(i) + "</a></li>";
    hub += "</ul><p>Short blurb.</p></body></html>";
    auto article = extract_article(hub, "https://example.com/");
    EXPECT(!article.is_article);
    EXPECT(!is_probably_readerable(hub));

    EXPECT(!extract_article("", "https://example.com/").is_article);
    EXPECT(!extract_article("<html><body><div id=app></div><script>render()</script></body></html>", "https://example.com/").is_article);

    EXPECT(is_probably_readerable(article_page()));
    // Prose hidden in unlikely regions doesn't count.
    std::string comments = "<html><body><div class=\"comments\">";
    for (int i = 0; i < 8; ++i)
        comments += paragraph(i);
    comments += "</div></body></html>";
    EXPECT(!is_probably_readerable(comments));
}

static void test_words_and_reading_time()
{
    EXPECT(count_words("") == 0);
    EXPECT(count_words("  one two\tthree\nfour  ") == 4);
    EXPECT(count_words("caf\xC3\xA9 na\xC3\xAFve") == 2);
    // Each CJK character counts as a word.
    EXPECT(count_words("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") == 3);
    EXPECT(count_words("hello \xE4\xB8\x96\xE7\x95\x8C") == 3);
    EXPECT(reading_time_minutes(0) == 1);
    EXPECT(reading_time_minutes(1) == 1);
    EXPECT(reading_time_minutes(238) == 1);
    EXPECT(reading_time_minutes(239) == 2);
    EXPECT(reading_time_minutes(2380) == 10);

    auto article = extract_article(article_page(), "https://example.com/");
    EXPECT(article.word_count > 200);
    EXPECT(article.reading_minutes == reading_time_minutes(article.word_count));
}

// ---- Reader document ----

static void test_reader_document()
{
    Article article;
    article.title = "</title><script>alert('title')</script>";
    article.byline = "<img src=x onerror=alert(1)>";
    article.site_name = "Site & Co";
    article.language = "en\" onload=\"alert(1)";
    article.content_html = "<p>Body</p>";
    article.word_count = 500;
    article.reading_minutes = 3;

    ReaderPreferences preferences;
    auto html = build_reader_document(article, "https://example.com/post?a=1&b=2", preferences);
    EXPECT(has(html, "Content-Security-Policy"));
    EXPECT(has(html, "script-src 'none'"));
    EXPECT(!has(lower(html), "<script"));
    EXPECT(!has(html, "<img src=x"));
    EXPECT(has(html, "&lt;/title&gt;&lt;script&gt;"));
    EXPECT(has(html, "Site &amp; Co"));
    EXPECT(has(html, "lang=\"en&quot; onload=&quot;alert(1)\""));
    EXPECT(has(html, "3 min read"));
    EXPECT(has(html, "href=\"https://example.com/post?a=1&amp;b=2\""));
    EXPECT(has(html, "prefers-color-scheme: dark"));
    EXPECT(has(html, "font-size:20px"));
    EXPECT(has(html, "line-height:1.6"));

    // A javascript: page URL gets no "View original" link.
    EXPECT(!has(build_reader_document(article, "javascript:alert(1)", preferences), "View original"));

    preferences.theme = ReaderTheme::Sepia;
    preferences.font = ReaderFont::Mono;
    preferences.width = ReaderWidth::Wide;
    auto sepia = build_reader_document(article, "https://example.com/", preferences);
    EXPECT(has(sepia, "#f4ecd8"));
    EXPECT(!has(sepia, "prefers-color-scheme"));
    EXPECT(has(sepia, "monospace"));
    EXPECT(has(sepia, "52em"));
}

static void test_reader_preferences()
{
    ReaderPreferences preferences;
    for (int i = 0; i < 20; ++i)
        preferences.increase_font_size();
    EXPECT(preferences.font_size == ReaderPreferences::MAXIMUM_FONT_SIZE);
    EXPECT(!preferences.can_increase_font_size());
    for (int i = 0; i < 20; ++i)
        preferences.decrease_font_size();
    EXPECT(preferences.font_size == ReaderPreferences::MINIMUM_FONT_SIZE);
    EXPECT(!preferences.can_decrease_font_size());

    ReaderPreferences cycling;
    auto start = cycling.line_height;
    int steps = 0;
    do {
        cycling.cycle_line_height();
        EXPECT(cycling.line_height >= ReaderPreferences::MINIMUM_LINE_HEIGHT && cycling.line_height <= ReaderPreferences::MAXIMUM_LINE_HEIGHT);
        ++steps;
    } while (cycling.line_height != start && steps < 20);
    EXPECT(steps == 6);
}

int main()
{
    test_parser_basics();
    test_parser_implied_end_tags_and_raw_text();
    test_parser_survives_hostile_input();
    test_url_resolution();
    test_url_scheme_filtering();
    test_extracts_article_and_metadata();
    test_title_and_site_fallbacks();
    test_byline_from_markup();
    test_scoring_prefers_dense_prose_over_link_lists();
    test_relative_urls_resolve_against_base();
    test_sanitizes_scripts_frames_forms_and_handlers();
    test_hidden_content_is_dropped();
    test_non_article_detection();
    test_words_and_reading_time();
    test_reader_document();
    test_reader_preferences();
    std::printf("All ReaderView unit tests passed!\n");
    return 0;
}
