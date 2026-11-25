/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/ReaderView/HtmlTree.h>

#include <algorithm>
#include <cstdint>

namespace Superbird::Reader {

namespace {

constexpr char ascii_lowercase(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

constexpr bool is_ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_ascii_hex_digit(char c) { return is_ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
constexpr bool is_html_whitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string lowercase(std::string_view input)
{
    std::string result(input);
    for (auto& c : result)
        c = ascii_lowercase(c);
    return result;
}

bool equals_ignoring_case(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (ascii_lowercase(a[i]) != ascii_lowercase(b[i]))
            return false;
    }
    return true;
}

void append_utf8(std::string& out, uint32_t code_point)
{
    if (code_point == 0 || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF))
        code_point = 0xFFFD;
    if (code_point < 0x80) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

struct NamedReference {
    std::string_view name;
    uint32_t code_point;
};

// The references articles commonly use. The browser's serializer only ever emits &amp; &lt; &gt; &quot; and &nbsp;,
// so this mostly matters for robustness.
constexpr NamedReference named_references[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
    { "ndash", 0x2013 }, { "mdash", 0x2014 }, { "hellip", 0x2026 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 },
    { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "laquo", 0xAB }, { "raquo", 0xBB }, { "copy", 0xA9 },
    { "reg", 0xAE }, { "trade", 0x2122 }, { "middot", 0xB7 }, { "bull", 0x2022 }, { "deg", 0xB0 },
    { "times", 0xD7 }, { "divide", 0xF7 }, { "euro", 0x20AC }, { "pound", 0xA3 }, { "yen", 0xA5 },
    { "cent", 0xA2 }, { "sect", 0xA7 }, { "para", 0xB6 }, { "shy", 0xAD }, { "zwj", 0x200D }, { "zwnj", 0x200C },
    { "thinsp", 0x2009 }, { "ensp", 0x2002 }, { "emsp", 0x2003 }, { "eacute", 0xE9 }, { "egrave", 0xE8 },
    { "aacute", 0xE1 }, { "agrave", 0xE0 }, { "iacute", 0xED }, { "oacute", 0xF3 }, { "uacute", 0xFA },
    { "ccedil", 0xE7 }, { "ntilde", 0xF1 }, { "auml", 0xE4 }, { "ouml", 0xF6 }, { "uuml", 0xFC }, { "szlig", 0xDF },
    { "atilde", 0xE3 }, { "otilde", 0xF5 }, { "acirc", 0xE2 }, { "ecirc", 0xEA }, { "ocirc", 0xF4 },
    { "hearts", 0x2665 }, { "rarr", 0x2192 }, { "larr", 0x2190 }, { "minus", 0x2212 }, { "prime", 0x2032 },
};

bool is_raw_text_element(std::string_view tag)
{
    return tag == "script" || tag == "style" || tag == "xmp" || tag == "iframe" || tag == "noembed" || tag == "noframes" || tag == "noscript";
}

bool is_escapable_raw_text_element(std::string_view tag)
{
    return tag == "title" || tag == "textarea";
}

// Elements whose start implicitly closes an open <p>.
bool closes_paragraph(std::string_view tag)
{
    static constexpr std::string_view tags[] = {
        "address", "article", "aside", "blockquote", "details", "dialog", "div", "dl", "fieldset", "figcaption",
        "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hgroup", "hr", "main", "menu",
        "nav", "ol", "p", "pre", "section", "summary", "table", "ul", "li", "dd", "dt",
    };
    return std::find(std::begin(tags), std::end(tags), tag) != std::end(tags);
}

bool is_scope_boundary(std::string_view tag)
{
    return tag == "html" || tag == "body" || tag == "table" || tag == "td" || tag == "th" || tag == "caption"
        || tag == "button" || tag == "object" || tag == "template" || tag == "marquee" || tag == "applet";
}

bool is_heading(std::string_view tag)
{
    return tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
}

class TreeBuilder {
public:
    TreeBuilder()
        : m_document(std::make_unique<Node>())
    {
        m_document->type = Node::Type::Document;
        m_stack.push_back(m_document.get());
    }

    std::unique_ptr<Node> take() { return std::move(m_document); }

    Node& current() { return *m_stack.back(); }

    void append_text(std::string text)
    {
        if (text.empty())
            return;
        auto& parent = current();
        if (!parent.children.empty() && parent.children.back()->is_text()) {
            parent.children.back()->text += text;
            return;
        }
        auto node = std::make_unique<Node>();
        node->type = Node::Type::Text;
        node->text = std::move(text);
        node->parent = &parent;
        parent.children.push_back(std::move(node));
    }

    void append_comment(std::string text)
    {
        auto node = std::make_unique<Node>();
        node->type = Node::Type::Comment;
        node->text = std::move(text);
        node->parent = &current();
        current().children.push_back(std::move(node));
    }

    // Returns the element it inserted, or nullptr if the tag merged into an existing <html>/<body>.
    Node* start_element(std::string name, std::vector<std::pair<std::string, std::string>> attributes, bool self_closing)
    {
        if (name == "html" || name == "body") {
            for (auto* open : m_stack) {
                if (open->is_element(name)) {
                    for (auto& attribute : attributes) {
                        if (!open->has_attribute(attribute.first))
                            open->attributes.push_back(std::move(attribute));
                    }
                    return nullptr;
                }
            }
        }

        apply_implied_end_tags(name);

        auto node = std::make_unique<Node>();
        node->type = Node::Type::Element;
        node->name = std::move(name);
        node->attributes = std::move(attributes);
        node->parent = &current();
        auto* raw = node.get();
        current().children.push_back(std::move(node));

        // Depth is capped: deeper elements still keep their place in document order, just not their nesting.
        if (!is_void_element(raw->name) && !self_closing && m_stack.size() < HTML_MAXIMUM_DEPTH)
            m_stack.push_back(raw);
        return raw;
    }

    void end_element(std::string_view name)
    {
        for (size_t i = m_stack.size(); i-- > 1;) {
            if (m_stack[i]->is_element(name)) {
                m_stack.resize(i);
                return;
            }
            // An end tag doesn't reach past a table cell or the like into the content around it.
            if (is_scope_boundary(m_stack[i]->name) && name != "table" && name != "td" && name != "th" && name != "body" && name != "html")
                return;
        }
    }

private:
    // Closes the nearest open element named `tag` if no boundary element intervenes. Returns whether it did.
    bool close_if_in_scope(std::string_view tag, std::initializer_list<std::string_view> extra_boundaries = {})
    {
        for (size_t i = m_stack.size(); i-- > 1;) {
            auto const& open_name = m_stack[i]->name;
            if (open_name == tag) {
                m_stack.resize(i);
                return true;
            }
            if (is_scope_boundary(open_name))
                return false;
            for (auto boundary : extra_boundaries) {
                if (open_name == boundary)
                    return false;
            }
        }
        return false;
    }

    void apply_implied_end_tags(std::string_view name)
    {
        if (closes_paragraph(name))
            close_if_in_scope("p");
        if (name == "li")
            close_if_in_scope("li", { "ul", "ol" });
        if (name == "dt" || name == "dd") {
            if (!close_if_in_scope("dt", { "dl" }))
                close_if_in_scope("dd", { "dl" });
        }
        if (name == "option")
            close_if_in_scope("option", { "select" });
        if (is_heading(name) && is_heading(current().name))
            m_stack.pop_back();
        if (name == "tr" || name == "tbody" || name == "thead" || name == "tfoot") {
            for (auto cell : { "td", "th" }) {
                for (size_t i = m_stack.size(); i-- > 1;) {
                    if (m_stack[i]->name == "table")
                        break;
                    if (m_stack[i]->name == cell) {
                        m_stack.resize(i);
                        break;
                    }
                }
            }
            if (m_stack.size() > 1 && current().name == "tr")
                m_stack.pop_back();
            if (name != "tr" && m_stack.size() > 1 && (current().name == "tbody" || current().name == "thead" || current().name == "tfoot"))
                m_stack.pop_back();
        }
        if (name == "td" || name == "th") {
            for (size_t i = m_stack.size(); i-- > 1;) {
                auto const& open_name = m_stack[i]->name;
                if (open_name == "tr" || open_name == "table")
                    break;
                if (open_name == "td" || open_name == "th") {
                    m_stack.resize(i);
                    break;
                }
            }
        }
    }

    std::unique_ptr<Node> m_document;
    std::vector<Node*> m_stack;
};

class Tokenizer {
public:
    Tokenizer(std::string_view input, TreeBuilder& builder)
        : m_input(input)
        , m_builder(builder)
    {
    }

    void run()
    {
        while (m_position < m_input.size()) {
            if (m_input[m_position] != '<') {
                auto end = m_input.find('<', m_position);
                if (end == std::string_view::npos)
                    end = m_input.size();
                m_builder.append_text(decode_html_entities(m_input.substr(m_position, end - m_position)));
                m_position = end;
                continue;
            }
            if (starts_with("<!--")) {
                auto end = m_input.find("-->", m_position + 4);
                auto text_end = end == std::string_view::npos ? m_input.size() : end;
                m_builder.append_comment(std::string(m_input.substr(m_position + 4, text_end - m_position - 4)));
                m_position = end == std::string_view::npos ? m_input.size() : end + 3;
                continue;
            }
            if (starts_with("<!") || starts_with("<?")) {
                skip_past('>');
                continue;
            }
            if (starts_with("</") && m_position + 2 < m_input.size() && is_ascii_alpha(m_input[m_position + 2])) {
                m_position += 2;
                auto name = read_tag_name();
                skip_past('>');
                m_builder.end_element(name);
                continue;
            }
            if (m_position + 1 < m_input.size() && is_ascii_alpha(m_input[m_position + 1])) {
                ++m_position;
                read_start_tag();
                continue;
            }
            m_builder.append_text("<");
            ++m_position;
        }
    }

private:
    bool starts_with(std::string_view prefix) const
    {
        return m_input.substr(m_position, prefix.size()) == prefix;
    }

    void skip_past(char c)
    {
        auto end = m_input.find(c, m_position);
        m_position = end == std::string_view::npos ? m_input.size() : end + 1;
    }

    void skip_whitespace()
    {
        while (m_position < m_input.size() && is_html_whitespace(m_input[m_position]))
            ++m_position;
    }

    std::string read_tag_name()
    {
        auto start = m_position;
        while (m_position < m_input.size()) {
            auto c = m_input[m_position];
            if (is_html_whitespace(c) || c == '>' || c == '/')
                break;
            ++m_position;
        }
        return lowercase(m_input.substr(start, m_position - start));
    }

    void read_start_tag()
    {
        auto name = read_tag_name();
        std::vector<std::pair<std::string, std::string>> attributes;
        bool self_closing = false;

        while (m_position < m_input.size()) {
            skip_whitespace();
            if (m_position >= m_input.size())
                break;
            auto c = m_input[m_position];
            if (c == '>') {
                ++m_position;
                break;
            }
            if (c == '/') {
                ++m_position;
                if (m_position < m_input.size() && m_input[m_position] == '>') {
                    self_closing = true;
                    ++m_position;
                    break;
                }
                continue;
            }

            auto name_start = m_position;
            while (m_position < m_input.size()) {
                auto d = m_input[m_position];
                if (is_html_whitespace(d) || d == '=' || d == '>' || (d == '/' && m_position > name_start))
                    break;
                ++m_position;
            }
            auto attribute_name = lowercase(m_input.substr(name_start, m_position - name_start));
            if (attribute_name.empty()) {
                ++m_position;
                continue;
            }

            std::string value;
            skip_whitespace();
            if (m_position < m_input.size() && m_input[m_position] == '=') {
                ++m_position;
                skip_whitespace();
                if (m_position < m_input.size() && (m_input[m_position] == '"' || m_input[m_position] == '\'')) {
                    auto quote = m_input[m_position++];
                    auto end = m_input.find(quote, m_position);
                    if (end == std::string_view::npos)
                        end = m_input.size();
                    value = decode_html_entities(m_input.substr(m_position, end - m_position));
                    m_position = std::min(end + 1, m_input.size());
                } else {
                    auto value_start = m_position;
                    while (m_position < m_input.size() && !is_html_whitespace(m_input[m_position]) && m_input[m_position] != '>')
                        ++m_position;
                    value = decode_html_entities(m_input.substr(value_start, m_position - value_start));
                }
            }

            auto duplicate = std::any_of(attributes.begin(), attributes.end(), [&](auto const& existing) { return existing.first == attribute_name; });
            if (!duplicate)
                attributes.emplace_back(std::move(attribute_name), std::move(value));
        }

        auto raw_text = is_raw_text_element(name);
        auto escapable_raw_text = is_escapable_raw_text_element(name);
        auto* element = m_builder.start_element(name, std::move(attributes), self_closing);
        if (!element || self_closing || (!raw_text && !escapable_raw_text))
            return;

        // Raw text runs to the matching end tag, whatever it contains.
        auto end = m_position;
        while (true) {
            end = m_input.find("</", end);
            if (end == std::string_view::npos) {
                end = m_input.size();
                break;
            }
            auto after = end + 2 + name.size();
            if (after <= m_input.size() && equals_ignoring_case(m_input.substr(end + 2, name.size()), name)
                && (after == m_input.size() || is_html_whitespace(m_input[after]) || m_input[after] == '>' || m_input[after] == '/'))
                break;
            end += 2;
        }
        auto content = m_input.substr(m_position, end - m_position);
        m_builder.append_text(escapable_raw_text ? decode_html_entities(content) : std::string(content));
        m_position = end;
        if (m_position < m_input.size()) {
            skip_past('>');
            m_builder.end_element(name);
        } else {
            m_builder.end_element(name);
        }
    }

    std::string_view m_input;
    size_t m_position { 0 };
    TreeBuilder& m_builder;
};

void append_collapsed(std::string& out, std::string_view text, bool& last_was_space)
{
    for (auto c : text) {
        if (is_html_whitespace(c)) {
            if (!last_was_space && !out.empty()) {
                out.push_back(' ');
                last_was_space = true;
            }
            continue;
        }
        out.push_back(c);
        last_was_space = false;
    }
}

bool is_invisible_text_container(Node const& node)
{
    return node.is_element("script") || node.is_element("style") || node.is_element("template") || node.is_element("noscript");
}

}

std::optional<std::string_view> Node::attribute(std::string_view attribute_name) const
{
    for (auto const& [key, value] : attributes) {
        if (key == attribute_name)
            return std::string_view(value);
    }
    return {};
}

void Node::append_raw_text(std::string& out) const
{
    if (is_text()) {
        out += text;
        return;
    }
    if (is_invisible_text_container(*this))
        return;
    for (auto const& child : children)
        child->append_raw_text(out);
}

std::string Node::text_content() const
{
    std::string raw;
    append_raw_text(raw);
    std::string collapsed;
    collapsed.reserve(raw.size());
    bool last_was_space = true;
    append_collapsed(collapsed, raw, last_was_space);
    while (!collapsed.empty() && collapsed.back() == ' ')
        collapsed.pop_back();
    return collapsed;
}

Node const* Node::find_first(std::string_view tag) const
{
    for (auto const& child : children) {
        if (child->is_element(tag))
            return child.get();
        if (auto const* found = child->find_first(tag))
            return found;
    }
    return nullptr;
}

bool is_void_element(std::string_view tag)
{
    static constexpr std::string_view tags[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr",
    };
    return std::find(std::begin(tags), std::end(tags), tag) != std::end(tags);
}

std::unique_ptr<Node> parse_html(std::string_view html)
{
    if (html.size() > HTML_MAXIMUM_INPUT_LENGTH)
        html = html.substr(0, HTML_MAXIMUM_INPUT_LENGTH);
    TreeBuilder builder;
    Tokenizer(html, builder).run();
    return builder.take();
}

std::string decode_html_entities(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    size_t i = 0;
    while (i < input.size()) {
        auto c = input[i];
        if (c != '&') {
            out.push_back(c);
            ++i;
            continue;
        }

        if (i + 1 < input.size() && input[i + 1] == '#') {
            size_t j = i + 2;
            bool hex = j < input.size() && (input[j] == 'x' || input[j] == 'X');
            if (hex)
                ++j;
            uint32_t value = 0;
            size_t digits = 0;
            while (j < input.size() && (hex ? is_ascii_hex_digit(input[j]) : is_ascii_digit(input[j]))) {
                auto d = input[j];
                uint32_t digit = is_ascii_digit(d) ? static_cast<uint32_t>(d - '0') : static_cast<uint32_t>(ascii_lowercase(d) - 'a' + 10);
                if (value <= 0x10FFFF)
                    value = value * (hex ? 16 : 10) + digit;
                ++digits;
                ++j;
            }
            if (digits == 0) {
                out.push_back('&');
                ++i;
                continue;
            }
            if (j < input.size() && input[j] == ';')
                ++j;
            append_utf8(out, value);
            i = j;
            continue;
        }

        size_t j = i + 1;
        while (j < input.size() && j - i <= 32 && (is_ascii_alpha(input[j]) || is_ascii_digit(input[j])))
            ++j;
        auto name = input.substr(i + 1, j - i - 1);
        bool decoded = false;
        for (auto const& reference : named_references) {
            if (reference.name == name) {
                append_utf8(out, reference.code_point);
                decoded = true;
                break;
            }
        }
        if (!decoded) {
            out.push_back('&');
            ++i;
            continue;
        }
        i = (j < input.size() && input[j] == ';') ? j + 1 : j;
    }
    return out;
}

std::string escape_html_text(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for (auto c : input) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out.push_back(c);
        }
    }
    return out;
}

std::string escape_html_attribute(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for (auto c : input) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out.push_back(c);
        }
    }
    return out;
}

}
