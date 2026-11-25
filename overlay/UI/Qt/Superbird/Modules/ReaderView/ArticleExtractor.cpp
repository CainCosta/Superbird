/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/ReaderView/ArticleExtractor.h>
#include <UI/Qt/Superbird/Modules/ReaderView/HtmlTree.h>
#include <UI/Qt/Superbird/Modules/ReaderView/UrlResolver.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Superbird::Reader {

namespace {

constexpr size_t MINIMUM_PARAGRAPH_LENGTH = 25;
constexpr size_t MINIMUM_ARTICLE_TEXT_LENGTH = 250;
constexpr size_t READERABLE_MINIMUM_NODE_LENGTH = 140;
constexpr double READERABLE_MINIMUM_SCORE = 20;
constexpr int WORDS_PER_MINUTE = 238;
constexpr size_t MAXIMUM_SERIALIZATION_DEPTH = HTML_MAXIMUM_DEPTH + 8;

constexpr char ascii_lowercase(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

std::string lowercase(std::string_view input)
{
    std::string result(input);
    for (auto& c : result)
        c = ascii_lowercase(c);
    return result;
}

bool contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

std::string_view trim(std::string_view text)
{
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; };
    while (!text.empty() && is_space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))
        text.remove_suffix(1);
    return text;
}

bool any_of_keywords(std::string_view text, std::initializer_list<std::string_view> keywords)
{
    for (auto keyword : keywords) {
        if (contains(text, keyword))
            return true;
    }
    return false;
}

std::string class_and_id(Node const& node)
{
    std::string combined;
    if (auto value = node.attribute("class"); value.has_value())
        combined += *value;
    combined += ' ';
    if (auto value = node.attribute("id"); value.has_value())
        combined += *value;
    return lowercase(combined);
}

bool is_unlikely_candidate(Node const& node)
{
    if (node.is_element("body") || node.is_element("html") || node.is_element("article") || node.is_element("main") || node.is_element("a"))
        return false;
    auto match = class_and_id(node);
    if (match.size() <= 1)
        return false;
    auto unlikely = any_of_keywords(match, {
                                               "-ad-", "ad-break", "agegate", "banner", "breadcrumb", "combx", "comment", "community", "cookie",
                                               "cover-wrap", "disqus", "extra", "footer", "gdpr", "header", "legends", "menu", "newsletter", "pager",
                                               "pagination", "popup", "promo", "related", "remark", "replies", "rss", "share", "shoutbox", "sidebar",
                                               "skyscraper", "social", "sponsor", "subscribe", "supplemental", "yom-remote",
                                           });
    if (!unlikely)
        return false;
    return !any_of_keywords(match, { "article", "body", "column", "content", "main", "shadow", "post", "entry", "story" });
}

int class_weight(Node const& node)
{
    auto match = class_and_id(node);
    if (match.size() <= 1)
        return 0;
    int weight = 0;
    if (any_of_keywords(match, { "article", "body", "content", "entry", "hentry", "h-entry", "main", "page", "post", "text", "blog", "story" }))
        weight += 25;
    if (any_of_keywords(match, { "-ad-", "hidden", "banner", "combx", "comment", "com-", "contact", "foot", "footnote", "gdpr", "masthead", "media", "meta", "outbrain", "promo", "related", "scroll", "share", "shoutbox", "sidebar", "skyscraper", "sponsor", "shopping", "tags", "tool", "widget" }))
        weight -= 25;
    return weight;
}

bool is_hidden(Node const& node)
{
    if (node.has_attribute("hidden"))
        return true;
    if (auto aria_hidden = node.attribute("aria-hidden"); aria_hidden.has_value() && lowercase(*aria_hidden) == "true")
        return true;
    if (auto style = node.attribute("style"); style.has_value()) {
        std::string compact;
        for (auto c : *style) {
            if (c != ' ' && c != '\t' && c != '\n')
                compact.push_back(ascii_lowercase(c));
        }
        if (contains(compact, "display:none") || contains(compact, "visibility:hidden"))
            return true;
    }
    return false;
}

bool has_chrome_role(Node const& node)
{
    auto role = node.attribute("role");
    if (!role.has_value())
        return false;
    auto value = lowercase(*role);
    return value == "navigation" || value == "complementary" || value == "contentinfo" || value == "dialog" || value == "alertdialog" || value == "menu" || value == "menubar" || value == "banner" || value == "search";
}

// Elements that never carry article content, and are dropped together with everything inside them.
bool is_never_content(std::string_view tag)
{
    static constexpr std::string_view tags[] = {
        "script", "style", "noscript", "template", "iframe", "frame", "frameset", "object", "embed", "applet", "link",
        "meta", "base", "svg", "math", "canvas", "video", "audio", "source", "track", "input", "button", "select",
        "textarea", "option", "optgroup", "datalist", "output", "progress", "meter", "dialog", "nav", "aside",
        "footer", "menu", "map", "area", "title", "head", "portal", "slot", "marquee", "param",
    };
    return std::find(std::begin(tags), std::end(tags), tag) != std::end(tags);
}

bool is_block_level(std::string_view tag)
{
    static constexpr std::string_view tags[] = {
        "address", "article", "aside", "blockquote", "details", "div", "dl", "fieldset", "figure", "footer", "form",
        "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "li", "main", "nav", "ol", "p", "pre", "section", "table",
        "ul", "img", "select",
    };
    return std::find(std::begin(tags), std::end(tags), tag) != std::end(tags);
}

// ---- Words & reading time ----

uint32_t decode_utf8(std::string_view text, size_t& index)
{
    auto lead = static_cast<unsigned char>(text[index]);
    size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2
        : (lead >> 4) == 0xE                              ? 3
        : (lead >> 3) == 0x1E                             ? 4
                                                          : 1;
    if (index + length > text.size()) {
        index = text.size();
        return 0xFFFD;
    }
    uint32_t code_point = length == 1 ? lead : length == 2 ? (lead & 0x1F)
        : length == 3                                      ? (lead & 0x0F)
                                                           : (lead & 0x07);
    for (size_t i = 1; i < length; ++i)
        code_point = (code_point << 6) | (static_cast<unsigned char>(text[index + i]) & 0x3F);
    index += length;
    return code_point;
}

bool is_cjk(uint32_t c)
{
    return (c >= 0x3040 && c <= 0x30FF) // Hiragana, Katakana
        || (c >= 0x3400 && c <= 0x4DBF) // CJK Extension A
        || (c >= 0x4E00 && c <= 0x9FFF) // CJK Unified Ideographs
        || (c >= 0xF900 && c <= 0xFAFF) // CJK Compatibility Ideographs
        || (c >= 0xAC00 && c <= 0xD7AF) // Hangul syllables
        || (c >= 0x20000 && c <= 0x2FA1F);
}

bool is_word_separator(uint32_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0xA0 || c == 0x2009 || c == 0x200B || c == 0x3000;
}

// ---- The extractor ----

class Extractor {
public:
    Extractor(std::string_view html, std::string_view page_url)
        : m_document(parse_html(html))
        , m_page_url(page_url)
    {
    }

    Article run()
    {
        Article article;
        read_document_metadata(article);
        prune(*m_document);

        auto const* body = m_document->find_first("body");
        if (!body)
            body = m_document.get();

        auto const* top = find_top_candidate(*body);
        std::vector<Node const*> content_nodes;
        if (top)
            content_nodes = gather_with_siblings(*top);
        else
            content_nodes.push_back(body);

        std::string text;
        for (auto const* node : content_nodes)
            text += node->text_content() + " ";
        auto trimmed_text = trim(text);

        // A candidate too thin to be the article falls back to the whole body, which still beats nothing.
        if (trimmed_text.size() < MINIMUM_ARTICLE_TEXT_LENGTH && top && body != top) {
            content_nodes = { body };
            text = body->text_content();
            trimmed_text = trim(text);
        }

        article.title = resolve_title();
        article.byline = find_byline(*body);
        article.site_name = resolve_site_name();
        article.excerpt = m_meta_description;

        std::string html_out;
        for (auto const* node : content_nodes)
            serialize(*node, html_out, 0, node == body || node == top);
        article.content_html = std::move(html_out);

        article.text_length = trimmed_text.size();
        article.word_count = count_words(trimmed_text);
        article.reading_minutes = reading_time_minutes(article.word_count);
        article.is_article = trimmed_text.size() >= MINIMUM_ARTICLE_TEXT_LENGTH && is_probably_readerable(*m_document);
        return article;
    }

private:
    bool is_removed(Node const& node) const { return m_removed.contains(&node); }

    void read_document_metadata(Article& article)
    {
        if (auto const* html = m_document->find_first("html")) {
            if (auto lang = html->attribute("lang"); lang.has_value())
                article.language = std::string(trim(*lang));
            if (auto dir = html->attribute("dir"); dir.has_value()) {
                auto value = lowercase(trim(*dir));
                if (value == "ltr" || value == "rtl")
                    article.direction = value;
            }
        }

        m_base_url = std::string(m_page_url);
        if (auto const* base = m_document->find_first("base")) {
            if (auto href = base->attribute("href"); href.has_value()) {
                if (auto resolved = resolve_url(m_page_url, *href); resolved.has_value()) {
                    auto scheme = url_scheme(*resolved);
                    if (scheme.has_value() && (*scheme == "http" || *scheme == "https"))
                        m_base_url = *resolved;
                }
            }
        }

        if (auto const* title = m_document->find_first("title"))
            m_document_title = title->text_content();

        collect_meta(*m_document);
    }

    void collect_meta(Node const& node)
    {
        for (auto const& child : node.children) {
            if (!child->is_element())
                continue;
            if (child->is_element("meta")) {
                auto key = lowercase(child->attribute("property").value_or(child->attribute("name").value_or("")));
                auto content = std::string(trim(child->attribute("content").value_or("")));
                if (content.empty())
                    continue;
                auto set_once = [&](std::string& field) {
                    if (field.empty())
                        field = decode_html_entities(content);
                };
                if (key == "og:title" || key == "twitter:title")
                    set_once(m_meta_title);
                else if (key == "og:site_name")
                    set_once(m_meta_site_name);
                else if (key == "author" || key == "dc.creator" || key == "article:author" || key == "byl" || key == "parsely-author")
                    set_once(m_meta_author);
                else if (key == "description" || key == "og:description" || key == "twitter:description")
                    set_once(m_meta_description);
                continue;
            }
            collect_meta(*child);
        }
    }

    // Marks chrome, hidden and unlikely regions so that scoring and serialization skip them.
    void prune(Node const& node)
    {
        for (auto const& child : node.children) {
            if (!child->is_element())
                continue;
            if (is_never_content(child->name) || is_hidden(*child) || has_chrome_role(*child) || is_unlikely_candidate(*child)) {
                m_removed.insert(child.get());
                continue;
            }
            prune(*child);
        }
    }

    std::string inner_text(Node const& node) const
    {
        std::string raw;
        append_visible_text(node, raw);
        std::string collapsed;
        bool last_space = true;
        for (auto c : raw) {
            bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
            if (space) {
                if (!last_space)
                    collapsed.push_back(' ');
                last_space = true;
            } else {
                collapsed.push_back(c);
                last_space = false;
            }
        }
        if (!collapsed.empty() && collapsed.back() == ' ')
            collapsed.pop_back();
        return collapsed;
    }

    void append_visible_text(Node const& node, std::string& out) const
    {
        if (node.is_text()) {
            out += node.text;
            return;
        }
        if (is_removed(node))
            return;
        for (auto const& child : node.children)
            append_visible_text(*child, out);
        if (node.is_element() && is_block_level(node.name))
            out.push_back(' ');
    }

    size_t link_text_length(Node const& node) const
    {
        if (is_removed(node))
            return 0;
        if (node.is_element("a"))
            return inner_text(node).size();
        size_t length = 0;
        for (auto const& child : node.children) {
            if (child->is_element())
                length += link_text_length(*child);
        }
        return length;
    }

    double link_density(Node const& node) const
    {
        auto text_length = inner_text(node).size();
        if (text_length == 0)
            return 0;
        return std::min(1.0, static_cast<double>(link_text_length(node)) / static_cast<double>(text_length));
    }

    bool has_block_children(Node const& node) const
    {
        for (auto const& child : node.children) {
            if (child->is_element() && !is_removed(*child) && (is_block_level(child->name) || has_block_children(*child)))
                return true;
        }
        return false;
    }

    double initial_score(Node const& node) const
    {
        double score = class_weight(node);
        auto const& tag = node.name;
        if (tag == "div" || tag == "article" || tag == "main" || tag == "section")
            score += 5;
        else if (tag == "pre" || tag == "td" || tag == "blockquote")
            score += 3;
        else if (tag == "address" || tag == "ol" || tag == "ul" || tag == "dl" || tag == "dd" || tag == "dt" || tag == "li" || tag == "form")
            score -= 3;
        else if (tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6" || tag == "th")
            score -= 5;
        if (tag == "article" || tag == "main")
            score += 10;
        return score;
    }

    void add_score(Node const& node, double amount)
    {
        auto it = m_scores.find(&node);
        if (it == m_scores.end())
            it = m_scores.emplace(&node, initial_score(node)).first;
        it->second += amount;
    }

    void collect_paragraphs(Node const& node, std::vector<Node const*>& paragraphs) const
    {
        for (auto const& child : node.children) {
            if (!child->is_element() || is_removed(*child))
                continue;
            auto const& tag = child->name;
            if (tag == "p" || tag == "pre" || tag == "td" || ((tag == "div" || tag == "section") && !has_block_children(*child)))
                paragraphs.push_back(child.get());
            collect_paragraphs(*child, paragraphs);
        }
    }

    Node const* find_top_candidate(Node const& body)
    {
        std::vector<Node const*> paragraphs;
        collect_paragraphs(body, paragraphs);

        for (auto const* paragraph : paragraphs) {
            auto text = inner_text(*paragraph);
            if (text.size() < MINIMUM_PARAGRAPH_LENGTH)
                continue;
            double score = 1 + static_cast<double>(std::count(text.begin(), text.end(), ','));
            score += std::min(static_cast<double>(text.size()) / 100.0, 3.0);

            auto const* ancestor = paragraph->parent;
            for (int level = 0; ancestor && level < 5; ++level, ancestor = ancestor->parent) {
                if (ancestor->type != Node::Type::Element)
                    break;
                double divider = level == 0 ? 1 : level == 1 ? 2
                                                             : level * 3;
                add_score(*ancestor, score / divider);
            }
        }

        Node const* top = nullptr;
        double top_score = 0;
        for (auto& [node, score] : m_scores) {
            auto final_score = score * (1 - link_density(*node));
            m_final_scores[node] = final_score;
            if (!top || final_score > top_score || (final_score == top_score && node < top)) {
                top = node;
                top_score = final_score;
            }
        }
        if (top && top_score <= 0)
            return nullptr;
        return top;
    }

    std::vector<Node const*> gather_with_siblings(Node const& top)
    {
        auto const* parent = top.parent;
        std::vector<Node const*> nodes;
        if (!parent) {
            nodes.push_back(&top);
            return nodes;
        }

        auto top_score = m_final_scores[&top];
        auto threshold = std::max(10.0, top_score * 0.2);
        auto top_class = top.attribute("class").value_or("");

        for (auto const& sibling : parent->children) {
            if (sibling.get() == &top) {
                nodes.push_back(&top);
                continue;
            }
            if (!sibling->is_element() || is_removed(*sibling))
                continue;
            double bonus = 0;
            if (!top_class.empty() && sibling->attribute("class").value_or("") == top_class)
                bonus += top_score * 0.2;
            if (auto it = m_final_scores.find(sibling.get()); it != m_final_scores.end() && it->second + bonus >= threshold) {
                nodes.push_back(sibling.get());
                continue;
            }
            if (sibling->is_element("p")) {
                auto text = inner_text(*sibling);
                auto density = link_density(*sibling);
                if ((text.size() > 80 && density < 0.25) || (!text.empty() && text.size() <= 80 && density == 0 && contains(text, ". ")))
                    nodes.push_back(sibling.get());
            }
        }
        return nodes;
    }

    std::string resolve_title() const
    {
        if (!m_meta_title.empty())
            return std::string(trim(m_meta_title));

        std::string title(trim(m_document_title));
        for (auto separator : { " | ", " - ", " \xE2\x80\x93 ", " \xE2\x80\x94 ", " :: ", " / ", " \xC2\xBB " }) {
            auto position = title.rfind(separator);
            if (position == std::string::npos)
                continue;
            auto head = std::string(trim(std::string_view(title).substr(0, position)));
            if (count_words(head) >= 3)
                return head;
            break;
        }
        if (title.empty()) {
            if (auto const* h1 = m_document->find_first("h1"))
                return h1->text_content();
        }
        return title;
    }

    std::string find_byline(Node const& body) const
    {
        if (!m_meta_author.empty() && !contains(m_meta_author, "://"))
            return clean_byline(m_meta_author);
        std::string found;
        find_byline_element(body, found);
        return clean_byline(found);
    }

    void find_byline_element(Node const& node, std::string& found) const
    {
        for (auto const& child : node.children) {
            if (!found.empty())
                return;
            if (!child->is_element())
                continue;
            auto rel = lowercase(child->attribute("rel").value_or(""));
            auto itemprop = lowercase(child->attribute("itemprop").value_or(""));
            auto match = class_and_id(*child);
            if (rel == "author" || contains(itemprop, "author") || contains(match, "byline") || contains(match, "author")) {
                auto text = child->text_content();
                if (text.size() >= 3 && text.size() <= 100) {
                    found = text;
                    return;
                }
            }
            find_byline_element(*child, found);
        }
    }

    static std::string clean_byline(std::string_view byline)
    {
        auto text = trim(byline);
        if (text.size() > 3 && (lowercase(text.substr(0, 3)) == "by "))
            text = trim(text.substr(3));
        return std::string(text);
    }

    std::string resolve_site_name() const
    {
        if (!m_meta_site_name.empty())
            return std::string(trim(m_meta_site_name));
        // The host of the page.
        auto url = std::string_view(m_page_url);
        auto scheme_end = url.find("://");
        if (scheme_end == std::string_view::npos)
            return {};
        auto host = url.substr(scheme_end + 3);
        host = host.substr(0, host.find_first_of("/?#"));
        if (auto at = host.rfind('@'); at != std::string_view::npos)
            host = host.substr(at + 1);
        host = host.substr(0, host.find(':'));
        if (host.substr(0, 4) == "www.")
            host.remove_prefix(4);
        return lowercase(host);
    }

    // ---- Sanitizing serializer ----

    enum class Treatment : unsigned char {
        Keep,
        Unwrap,
        UnwrapAsBlock,
        Drop,
    };

    static Treatment treatment_for(std::string_view tag)
    {
        static constexpr std::string_view kept[] = {
            "p", "br", "hr", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "dl", "dt", "dd", "blockquote", "pre",
            "code", "em", "strong", "b", "i", "u", "s", "sub", "sup", "small", "mark", "abbr", "q", "cite", "time",
            "del", "ins", "kbd", "var", "samp", "figure", "figcaption", "table", "thead", "tbody", "tfoot", "tr", "th",
            "td", "caption", "a", "img", "details", "summary",
        };
        static constexpr std::string_view block_containers[] = {
            "div", "section", "article", "main", "header", "hgroup", "address", "center", "fieldset", "form",
        };
        if (is_never_content(tag))
            return Treatment::Drop;
        if (std::find(std::begin(kept), std::end(kept), tag) != std::end(kept))
            return Treatment::Keep;
        if (std::find(std::begin(block_containers), std::end(block_containers), tag) != std::end(block_containers))
            return Treatment::UnwrapAsBlock;
        // h1 becomes h2 below (the reader header owns the one h1); anything else, including custom elements, keeps
        // only its content.
        return Treatment::Unwrap;
    }

    static bool is_digits(std::string_view value)
    {
        value = trim(value);
        return !value.empty() && value.size() <= 6 && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    static bool is_placeholder_image(std::string_view src)
    {
        auto scheme = url_scheme(src);
        return src.empty() || (scheme.has_value() && *scheme == "data");
    }

    void serialize(Node const& node, std::string& out, size_t depth, bool is_root)
    {
        if (depth > MAXIMUM_SERIALIZATION_DEPTH)
            return;
        if (node.is_text()) {
            out += escape_html_text(node.text);
            return;
        }
        if (node.type == Node::Type::Comment)
            return;
        if (node.type == Node::Type::Document) {
            for (auto const& child : node.children)
                serialize(*child, out, depth + 1, false);
            return;
        }
        if (is_removed(node))
            return;

        // The title already heads the page; drop a heading that repeats it.
        if ((node.is_element("h1") || node.is_element("h2")) && !m_dropped_title_heading) {
            auto text = node.text_content();
            if (!text.empty() && lowercase(text) == lowercase(resolve_title_cached())) {
                m_dropped_title_heading = true;
                return;
            }
        }

        auto tag = node.is_element("h1") ? std::string("h2") : node.name;
        auto treatment = is_root && node.name != "body" ? Treatment::UnwrapAsBlock : treatment_for(tag);
        if (node.is_element("body"))
            treatment = Treatment::Unwrap;

        switch (treatment) {
        case Treatment::Drop:
            return;
        case Treatment::Unwrap:
            for (auto const& child : node.children)
                serialize(*child, out, depth + 1, false);
            return;
        case Treatment::UnwrapAsBlock:
            out += "<div>";
            for (auto const& child : node.children)
                serialize(*child, out, depth + 1, false);
            out += "</div>";
            return;
        case Treatment::Keep:
            break;
        }

        std::string attributes;
        if (tag == "a") {
            auto href = node.attribute("href");
            std::optional<std::string> safe = href.has_value() ? safe_link_url(m_base_url, *href) : std::nullopt;
            if (!safe.has_value()) {
                // A link to somewhere Reader View won't go keeps its words but not its target.
                for (auto const& child : node.children)
                    serialize(*child, out, depth + 1, false);
                return;
            }
            attributes += " href=\"" + escape_html_attribute(*safe) + "\"";
            if (auto title = node.attribute("title"); title.has_value())
                attributes += " title=\"" + escape_html_attribute(*title) + "\"";
        } else if (tag == "img") {
            std::optional<std::string> source;
            auto src = node.attribute("src").value_or("");
            if (!is_placeholder_image(src))
                source = safe_image_url(m_base_url, src);
            for (auto lazy : { "data-src", "data-lazy-src", "data-original" }) {
                if (source.has_value())
                    break;
                if (auto value = node.attribute(lazy); value.has_value())
                    source = safe_image_url(m_base_url, *value);
            }
            if (!source.has_value())
                return;
            attributes += " src=\"" + escape_html_attribute(*source) + "\"";
            attributes += " alt=\"" + escape_html_attribute(node.attribute("alt").value_or("")) + "\"";
            for (auto dimension : { "width", "height" }) {
                if (auto value = node.attribute(dimension); value.has_value() && is_digits(*value))
                    attributes += std::string(" ") + dimension + "=\"" + std::string(trim(*value)) + "\"";
            }
            attributes += " loading=\"lazy\"";
        } else if (tag == "td" || tag == "th") {
            for (auto span : { "colspan", "rowspan" }) {
                if (auto value = node.attribute(span); value.has_value() && is_digits(*value))
                    attributes += std::string(" ") + span + "=\"" + std::string(trim(*value)) + "\"";
            }
        } else if (tag == "ol") {
            if (auto value = node.attribute("start"); value.has_value() && is_digits(*value))
                attributes += " start=\"" + std::string(trim(*value)) + "\"";
        } else if (tag == "abbr") {
            if (auto title = node.attribute("title"); title.has_value())
                attributes += " title=\"" + escape_html_attribute(*title) + "\"";
        }

        out += "<" + tag + attributes + ">";
        if (is_void_element(tag))
            return;
        for (auto const& child : node.children)
            serialize(*child, out, depth + 1, false);
        out += "</" + tag + ">";
    }

    std::string const& resolve_title_cached()
    {
        if (!m_title_cache.has_value())
            m_title_cache = resolve_title();
        return *m_title_cache;
    }

    std::unique_ptr<Node> m_document;
    std::string_view m_page_url;
    std::string m_base_url;
    std::string m_document_title;
    std::string m_meta_title;
    std::string m_meta_site_name;
    std::string m_meta_author;
    std::string m_meta_description;
    std::unordered_set<Node const*> m_removed;
    std::unordered_map<Node const*, double> m_scores;
    std::unordered_map<Node const*, double> m_final_scores;
    std::optional<std::string> m_title_cache;
    bool m_dropped_title_heading { false };
};

bool is_unlikely_or_hidden(Node const& node)
{
    return is_never_content(node.name) || is_hidden(node) || has_chrome_role(node) || is_unlikely_candidate(node);
}

void readerable_score(Node const& node, double& score)
{
    for (auto const& child : node.children) {
        if (!child->is_element() || is_unlikely_or_hidden(*child))
            continue;
        if (child->is_element("p") || child->is_element("pre") || child->is_element("article")) {
            auto length = child->text_content().size();
            if (length >= READERABLE_MINIMUM_NODE_LENGTH)
                score += std::sqrt(static_cast<double>(length - READERABLE_MINIMUM_NODE_LENGTH));
            if (score > READERABLE_MINIMUM_SCORE)
                return;
            // An <article>'s paragraphs were just counted as part of it.
            if (child->is_element("article"))
                continue;
        }
        readerable_score(*child, score);
        if (score > READERABLE_MINIMUM_SCORE)
            return;
    }
}

}

Article extract_article(std::string_view html, std::string_view page_url)
{
    return Extractor(html, page_url).run();
}

bool is_probably_readerable(Node const& document)
{
    double score = 0;
    readerable_score(document, score);
    return score > READERABLE_MINIMUM_SCORE;
}

bool is_probably_readerable(std::string_view html)
{
    auto document = parse_html(html);
    return is_probably_readerable(*document);
}

size_t count_words(std::string_view text)
{
    size_t words = 0;
    bool in_word = false;
    size_t index = 0;
    while (index < text.size()) {
        auto c = decode_utf8(text, index);
        if (is_cjk(c)) {
            ++words;
            in_word = false;
            continue;
        }
        if (is_word_separator(c)) {
            in_word = false;
            continue;
        }
        if (!in_word) {
            ++words;
            in_word = true;
        }
    }
    return words;
}

int reading_time_minutes(size_t words)
{
    if (words == 0)
        return 1;
    return static_cast<int>((words + WORDS_PER_MINUTE - 1) / WORDS_PER_MINUTE);
}

}
