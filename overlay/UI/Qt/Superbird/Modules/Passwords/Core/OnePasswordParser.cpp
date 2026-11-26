/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/OnePasswordParser.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <variant>

namespace Superbird::Passwords {

namespace {

struct JsonNode {
    using Object = std::map<std::string, JsonNode, std::less<>>;
    using Array = std::vector<JsonNode>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value { nullptr };

    bool is_object() const { return std::holds_alternative<Object>(value); }
    bool is_array() const { return std::holds_alternative<Array>(value); }
    bool is_string() const { return std::holds_alternative<std::string>(value); }
    bool is_bool() const { return std::holds_alternative<bool>(value); }

    Object const* as_object() const { return std::get_if<Object>(&value); }
    Array const* as_array() const { return std::get_if<Array>(&value); }
    std::string const* as_string() const { return std::get_if<std::string>(&value); }
    bool as_bool(bool fallback = false) const
    {
        if (auto const* b = std::get_if<bool>(&value))
            return *b;
        return fallback;
    }

    JsonNode const* get(std::string_view key) const
    {
        auto const* obj = as_object();
        if (!obj)
            return nullptr;
        auto it = obj->find(key);
        return it == obj->end() ? nullptr : &it->second;
    }

    std::string get_string(std::string_view key) const
    {
        auto const* n = get(key);
        if (!n || !n->is_string())
            return {};
        return *n->as_string();
    }
};

class MiniJsonParser {
public:
    explicit MiniJsonParser(std::string_view input)
        : m_input(input)
    {
    }

    std::optional<JsonNode> parse()
    {
        skip_ws();
        auto node = parse_value();
        if (!node)
            return std::nullopt;
        skip_ws();
        if (m_pos != m_input.size())
            return std::nullopt;
        return node;
    }

private:
    void skip_ws()
    {
        while (m_pos < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_pos])))
            ++m_pos;
    }

    bool consume(char expected)
    {
        if (m_pos < m_input.size() && m_input[m_pos] == expected) {
            ++m_pos;
            return true;
        }
        return false;
    }

    std::optional<JsonNode> parse_value()
    {
        skip_ws();
        if (m_pos >= m_input.size())
            return std::nullopt;
        char c = m_input[m_pos];
        if (c == '{')
            return parse_object();
        if (c == '[')
            return parse_array();
        if (c == '"') {
            auto s = parse_string();
            if (!s)
                return std::nullopt;
            return JsonNode { std::move(*s) };
        }
        if (c == 't' && m_input.substr(m_pos, 4) == "true") {
            m_pos += 4;
            return JsonNode { true };
        }
        if (c == 'f' && m_input.substr(m_pos, 5) == "false") {
            m_pos += 5;
            return JsonNode { false };
        }
        if (c == 'n' && m_input.substr(m_pos, 4) == "null") {
            m_pos += 4;
            return JsonNode { nullptr };
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return parse_number();
        return std::nullopt;
    }

    std::optional<JsonNode> parse_object()
    {
        if (!consume('{'))
            return std::nullopt;
        JsonNode::Object obj;
        skip_ws();
        if (consume('}'))
            return JsonNode { std::move(obj) };
        while (true) {
            skip_ws();
            auto key = parse_string();
            if (!key)
                return std::nullopt;
            skip_ws();
            if (!consume(':'))
                return std::nullopt;
            auto val = parse_value();
            if (!val)
                return std::nullopt;
            obj.emplace(std::move(*key), std::move(*val));
            skip_ws();
            if (consume('}'))
                return JsonNode { std::move(obj) };
            if (!consume(','))
                return std::nullopt;
        }
    }

    std::optional<JsonNode> parse_array()
    {
        if (!consume('['))
            return std::nullopt;
        JsonNode::Array arr;
        skip_ws();
        if (consume(']'))
            return JsonNode { std::move(arr) };
        while (true) {
            auto val = parse_value();
            if (!val)
                return std::nullopt;
            arr.push_back(std::move(*val));
            skip_ws();
            if (consume(']'))
                return JsonNode { std::move(arr) };
            if (!consume(','))
                return std::nullopt;
        }
    }

    static void append_utf8(std::string& out, uint32_t cp)
    {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::optional<uint32_t> parse_hex4()
    {
        if (m_pos + 4 > m_input.size())
            return std::nullopt;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = m_input[m_pos++];
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v |= static_cast<uint32_t>(c - 'A' + 10);
            else
                return std::nullopt;
        }
        return v;
    }

    std::optional<std::string> parse_string()
    {
        if (!consume('"'))
            return std::nullopt;
        std::string out;
        while (m_pos < m_input.size()) {
            char c = m_input[m_pos++];
            if (c == '"')
                return out;
            if (c == '\\') {
                if (m_pos >= m_input.size())
                    return std::nullopt;
                char esc = m_input[m_pos++];
                switch (esc) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(esc);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    auto cp = parse_hex4();
                    if (!cp)
                        return std::nullopt;
                    if (*cp >= 0xD800 && *cp <= 0xDBFF && m_pos + 2 <= m_input.size()
                        && m_input[m_pos] == '\\' && m_input[m_pos + 1] == 'u') {
                        m_pos += 2;
                        auto low = parse_hex4();
                        if (!low || *low < 0xDC00 || *low > 0xDFFF)
                            return std::nullopt;
                        uint32_t full = 0x10000 + (((*cp - 0xD800) << 10) | (*low - 0xDC00));
                        append_utf8(out, full);
                    } else {
                        append_utf8(out, *cp);
                    }
                    break;
                }
                default:
                    return std::nullopt;
                }
            } else {
                out.push_back(c);
            }
        }
        return std::nullopt;
    }

    std::optional<JsonNode> parse_number()
    {
        size_t start = m_pos;
        if (m_pos < m_input.size() && m_input[m_pos] == '-')
            ++m_pos;
        if (m_pos >= m_input.size() || !std::isdigit(static_cast<unsigned char>(m_input[m_pos])))
            return std::nullopt;
        while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])))
            ++m_pos;
        if (m_pos < m_input.size() && m_input[m_pos] == '.') {
            ++m_pos;
            while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])))
                ++m_pos;
        }
        if (m_pos < m_input.size() && (m_input[m_pos] == 'e' || m_input[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_input.size() && (m_input[m_pos] == '+' || m_input[m_pos] == '-'))
                ++m_pos;
            while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])))
                ++m_pos;
        }
        std::string num_str(m_input.substr(start, m_pos - start));
        char* end_ptr = nullptr;
        double val = std::strtod(num_str.c_str(), &end_ptr);
        if (end_ptr != num_str.c_str() + num_str.size())
            return std::nullopt;
        return JsonNode { val };
    }

    std::string_view m_input;
    size_t m_pos { 0 };
};

std::string ascii_upper(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return out;
}

} // namespace

std::optional<std::vector<CredentialSummary>> OnePasswordParser::parse_item_list(
    std::string_view json,
    std::string_view account_id)
{
    MiniJsonParser parser(json);
    auto root = parser.parse();
    if (!root || !root->is_array())
        return std::nullopt;

    std::vector<CredentialSummary> items;
    for (auto const& entry : *root->as_array()) {
        if (!entry.is_object())
            continue;
        auto id = entry.get_string("id");
        if (id.empty())
            continue;

        CredentialSummary summary;
        summary.provider = ProviderKind::OnePassword;
        summary.provider_account_id = std::string(account_id);
        summary.id = std::move(id);
        summary.name = entry.get_string("title");
        summary.username = entry.get_string("additional_information");
        summary.has_password = true;

        if (auto const* fav = entry.get("favorite"))
            summary.favorite = fav->as_bool(false);

        if (auto const* urls = entry.get("urls"); urls && urls->is_array()) {
            for (auto const& url_obj : *urls->as_array()) {
                auto href = url_obj.get_string("href");
                if (!href.empty())
                    summary.uris.push_back({ std::move(href), UriMatchType::BaseDomain });
            }
        }

        if (auto const* tags = entry.get("tags"); tags && tags->is_array()) {
            for (auto const& tag : *tags->as_array()) {
                if (tag.is_string() && ascii_upper(*tag.as_string()) == "FAVORITE")
                    summary.favorite = true;
            }
        }

        items.push_back(std::move(summary));
    }
    return items;
}

std::optional<CredentialSecret> OnePasswordParser::parse_item_detail(std::string_view json)
{
    MiniJsonParser parser(json);
    auto root = parser.parse();
    if (!root || !root->is_object())
        return std::nullopt;

    CredentialSecret secret;
    if (auto const* fields = root->get("fields"); fields && fields->is_array()) {
        for (auto const& field : *fields->as_array()) {
            if (!field.is_object())
                continue;
            auto purpose = ascii_upper(field.get_string("purpose"));
            auto type = ascii_upper(field.get_string("type"));
            auto id = ascii_upper(field.get_string("id"));
            auto value = field.get_string("value");

            if (purpose == "USERNAME" || (secret.username.empty() && id == "USERNAME")) {
                secret.username = std::move(value);
            } else if (purpose == "PASSWORD" || (secret.password.empty() && id == "PASSWORD" && type == "CONCEALED")) {
                secret.password = std::move(value);
            } else if (purpose == "NOTES" && !value.empty()) {
                secret.notes = std::move(value);
            } else if (type == "OTP") {
                auto totp_code = field.get_string("totp");
                if (!totp_code.empty())
                    secret.precomputed_totp = std::move(totp_code);
                if (!value.empty())
                    secret.totp_uri = std::move(value);
            }
        }
    }

    if (secret.username.empty())
        secret.username = root->get_string("additional_information");

    return secret;
}

std::optional<std::string> OnePasswordParser::parse_otp_output(std::string_view output)
{
    while (!output.empty() && std::isspace(static_cast<unsigned char>(output.front())))
        output.remove_prefix(1);
    while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back())))
        output.remove_suffix(1);
    if (output.size() < 6 || output.size() > 8)
        return std::nullopt;
    for (char c : output) {
        if (c < '0' || c > '9')
            return std::nullopt;
    }
    return std::string(output);
}

}
