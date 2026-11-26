/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenProtocol.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <variant>

namespace Superbird::Passwords {

namespace {

struct BwJson {
    using Object = std::map<std::string, BwJson, std::less<>>;
    using Array = std::vector<BwJson>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> val { nullptr };

    bool is_object() const { return std::holds_alternative<Object>(val); }
    bool is_array() const { return std::holds_alternative<Array>(val); }
    bool is_string() const { return std::holds_alternative<std::string>(val); }
    bool is_number() const { return std::holds_alternative<double>(val); }
    bool is_null() const { return std::holds_alternative<std::nullptr_t>(val); }

    Object const* as_object() const { return std::get_if<Object>(&val); }
    Array const* as_array() const { return std::get_if<Array>(&val); }
    std::string const* as_string() const { return std::get_if<std::string>(&val); }

    BwJson const* get_ci(std::string_view key) const
    {
        auto const* obj = as_object();
        if (!obj)
            return nullptr;
        if (auto it = obj->find(key); it != obj->end())
            return &it->second;
        // Bitwarden & Vaultwarden sometimes vary casing (`kdf` vs `Kdf`, `id` vs `Id`).
        for (auto const& [k, v] : *obj) {
            if (k.size() != key.size())
                continue;
            bool match = true;
            for (size_t i = 0; i < k.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(k[i])) != std::tolower(static_cast<unsigned char>(key[i]))) {
                    match = false;
                    break;
                }
            }
            if (match)
                return &v;
        }
        return nullptr;
    }

    std::string get_string(std::string_view key) const
    {
        auto const* n = get_ci(key);
        if (!n || !n->is_string())
            return {};
        return *n->as_string();
    }

    std::optional<int64_t> get_int(std::string_view key) const
    {
        auto const* n = get_ci(key);
        if (!n)
            return std::nullopt;
        if (auto const* d = std::get_if<double>(&n->val))
            return static_cast<int64_t>(*d);
        if (auto const* s = std::get_if<std::string>(&n->val)) {
            if (s->empty())
                return std::nullopt;
            char* end_ptr = nullptr;
            long long parsed = std::strtoll(s->c_str(), &end_ptr, 10);
            if (end_ptr == s->c_str() + s->size())
                return static_cast<int64_t>(parsed);
            return std::nullopt;
        }
        return std::nullopt;
    }

    bool get_bool(std::string_view key, bool fallback = false) const
    {
        auto const* n = get_ci(key);
        if (!n)
            return fallback;
        if (auto const* b = std::get_if<bool>(&n->val))
            return *b;
        return fallback;
    }
};

class BwJsonParser {
public:
    explicit BwJsonParser(std::string_view s)
        : m_s(s)
    {
    }

    std::optional<BwJson> parse()
    {
        skip_ws();
        auto r = parse_value();
        if (!r)
            return std::nullopt;
        skip_ws();
        if (m_p != m_s.size())
            return std::nullopt;
        return r;
    }

private:
    void skip_ws()
    {
        while (m_p < m_s.size() && std::isspace(static_cast<unsigned char>(m_s[m_p])))
            ++m_p;
    }

    bool take(char c)
    {
        if (m_p < m_s.size() && m_s[m_p] == c) {
            ++m_p;
            return true;
        }
        return false;
    }

    std::optional<BwJson> parse_value()
    {
        skip_ws();
        if (m_p >= m_s.size())
            return std::nullopt;
        char c = m_s[m_p];
        if (c == '{')
            return parse_object();
        if (c == '[')
            return parse_array();
        if (c == '"') {
            auto s = parse_string();
            if (!s)
                return std::nullopt;
            return BwJson { std::move(*s) };
        }
        if (c == 't' && m_s.substr(m_p, 4) == "true") {
            m_p += 4;
            return BwJson { true };
        }
        if (c == 'f' && m_s.substr(m_p, 5) == "false") {
            m_p += 5;
            return BwJson { false };
        }
        if (c == 'n' && m_s.substr(m_p, 4) == "null") {
            m_p += 4;
            return BwJson { nullptr };
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return parse_number();
        return std::nullopt;
    }

    std::optional<BwJson> parse_object()
    {
        if (!take('{'))
            return std::nullopt;
        BwJson::Object obj;
        skip_ws();
        if (take('}'))
            return BwJson { std::move(obj) };
        while (true) {
            skip_ws();
            auto k = parse_string();
            if (!k)
                return std::nullopt;
            skip_ws();
            if (!take(':'))
                return std::nullopt;
            auto v = parse_value();
            if (!v)
                return std::nullopt;
            obj.emplace(std::move(*k), std::move(*v));
            skip_ws();
            if (take('}'))
                return BwJson { std::move(obj) };
            if (!take(','))
                return std::nullopt;
        }
    }

    std::optional<BwJson> parse_array()
    {
        if (!take('['))
            return std::nullopt;
        BwJson::Array arr;
        skip_ws();
        if (take(']'))
            return BwJson { std::move(arr) };
        while (true) {
            auto v = parse_value();
            if (!v)
                return std::nullopt;
            arr.push_back(std::move(*v));
            skip_ws();
            if (take(']'))
                return BwJson { std::move(arr) };
            if (!take(','))
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
        if (m_p + 4 > m_s.size())
            return std::nullopt;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = m_s[m_p++];
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
        if (!take('"'))
            return std::nullopt;
        std::string out;
        while (m_p < m_s.size()) {
            char c = m_s[m_p++];
            if (c == '"')
                return out;
            if (c == '\\') {
                if (m_p >= m_s.size())
                    return std::nullopt;
                char esc = m_s[m_p++];
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
                    if (*cp >= 0xD800 && *cp <= 0xDBFF && m_p + 2 <= m_s.size()
                        && m_s[m_p] == '\\' && m_s[m_p + 1] == 'u') {
                        m_p += 2;
                        auto low = parse_hex4();
                        if (!low || *low < 0xDC00 || *low > 0xDFFF)
                            return std::nullopt;
                        append_utf8(out, 0x10000 + (((*cp - 0xD800) << 10) | (*low - 0xDC00)));
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

    std::optional<BwJson> parse_number()
    {
        size_t start = m_p;
        if (m_p < m_s.size() && m_s[m_p] == '-')
            ++m_p;
        if (m_p >= m_s.size() || !std::isdigit(static_cast<unsigned char>(m_s[m_p])))
            return std::nullopt;
        while (m_p < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_p])))
            ++m_p;
        if (m_p < m_s.size() && m_s[m_p] == '.') {
            ++m_p;
            while (m_p < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_p])))
                ++m_p;
        }
        if (m_p < m_s.size() && (m_s[m_p] == 'e' || m_s[m_p] == 'E')) {
            ++m_p;
            if (m_p < m_s.size() && (m_s[m_p] == '+' || m_s[m_p] == '-'))
                ++m_p;
            while (m_p < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_p])))
                ++m_p;
        }
        std::string num_str(m_s.substr(start, m_p - start));
        char* end_ptr = nullptr;
        double val = std::strtod(num_str.c_str(), &end_ptr);
        if (end_ptr != num_str.c_str() + num_str.size())
            return std::nullopt;
        return BwJson { val };
    }

    std::string_view m_s;
    size_t m_p { 0 };
};

std::string trim_trailing_slash(std::string_view url)
{
    while (!url.empty() && url.back() == '/')
        url.remove_suffix(1);
    return std::string(url);
}

BitwardenRawCipher parse_cipher_node(BwJson const& obj)
{
    BitwardenRawCipher c;
    c.id = obj.get_string("id");
    c.organization_id = obj.get_string("organizationId");
    c.type = static_cast<int>(obj.get_int("type").value_or(1));
    c.encrypted_key = obj.get_string("key");
    c.encrypted_name = obj.get_string("name");
    c.encrypted_notes = obj.get_string("notes");
    c.favorite = obj.get_bool("favorite", false);
    if (auto const* del = obj.get_ci("deletedDate"); del && !del->is_null() && del->is_string() && !del->as_string()->empty())
        c.deleted = true;

    if (auto const* login = obj.get_ci("login"); login && login->is_object()) {
        c.encrypted_username = login->get_string("username");
        c.encrypted_password = login->get_string("password");
        c.encrypted_totp = login->get_string("totp");
        if (auto const* uris = login->get_ci("uris"); uris && uris->is_array()) {
            for (auto const& u : *uris->as_array()) {
                if (!u.is_object())
                    continue;
                auto uri_str = u.get_string("uri");
                if (uri_str.empty())
                    continue;
                auto match = static_cast<int>(u.get_int("match").value_or(0));
                UriMatchType mt = UriMatchType::BaseDomain;
                if (match >= 0 && match <= 5)
                    mt = static_cast<UriMatchType>(match);
                c.uris.push_back({ std::move(uri_str), mt });
            }
        }
    }
    return c;
}

std::optional<std::string> build_encrypted_cipher_json(
    NewLogin const& login,
    BitwardenSymmetricKey const& user_key)
{
    auto enc_name = BitwardenCrypto::encrypt_string(login.name, user_key);
    auto enc_user = login.username.empty() ? std::optional<std::string>("") : BitwardenCrypto::encrypt_string(login.username, user_key);
    auto enc_pass = login.password.empty() ? std::optional<std::string>("") : BitwardenCrypto::encrypt_string(login.password, user_key);
    if (!enc_name || !enc_user || !enc_pass)
        return std::nullopt;

    std::string enc_totp;
    if (login.totp_uri.has_value() && !login.totp_uri->empty()) {
        auto r = BitwardenCrypto::encrypt_string(*login.totp_uri, user_key);
        if (!r)
            return std::nullopt;
        enc_totp = std::move(*r);
    }

    std::string enc_notes;
    if (login.notes.has_value() && !login.notes->empty()) {
        auto r = BitwardenCrypto::encrypt_string(*login.notes, user_key);
        if (!r)
            return std::nullopt;
        enc_notes = std::move(*r);
    }

    std::string uris_json = "[";
    for (size_t i = 0; i < login.uris.size(); ++i) {
        auto enc_uri = BitwardenCrypto::encrypt_string(login.uris[i].uri, user_key);
        if (!enc_uri)
            return std::nullopt;
        if (i > 0)
            uris_json += ",";
        uris_json += "{\"uri\":\"" + BitwardenProtocol::json_escape(*enc_uri) + "\",\"match\":" + std::to_string(static_cast<int>(login.uris[i].match_type)) + "}";
    }
    uris_json += "]";

    std::string json = "{";
    json += "\"type\":1,";
    json += "\"organizationId\":null,";
    json += "\"folderId\":null,";
    json += "\"name\":\"" + BitwardenProtocol::json_escape(*enc_name) + "\",";
    json += "\"notes\":" + (enc_notes.empty() ? std::string("null") : ("\"" + BitwardenProtocol::json_escape(enc_notes) + "\"")) + ",";
    json += "\"favorite\":" + std::string(login.favorite ? "true" : "false") + ",";
    json += "\"reprompt\":0,";
    json += "\"login\":{";
    json += "\"username\":" + (enc_user->empty() ? std::string("null") : ("\"" + BitwardenProtocol::json_escape(*enc_user) + "\"")) + ",";
    json += "\"password\":" + (enc_pass->empty() ? std::string("null") : ("\"" + BitwardenProtocol::json_escape(*enc_pass) + "\"")) + ",";
    json += "\"totp\":" + (enc_totp.empty() ? std::string("null") : ("\"" + BitwardenProtocol::json_escape(enc_totp) + "\"")) + ",";
    json += "\"uris\":" + uris_json;
    json += "}}";
    return json;
}

} // namespace

BitwardenEndpoints BitwardenEndpoints::for_region(BitwardenRegion region, std::string_view custom_base_url)
{
    switch (region) {
    case BitwardenRegion::US:
        return { "https://identity.bitwarden.com", "https://api.bitwarden.com" };
    case BitwardenRegion::EU:
        return { "https://identity.bitwarden.eu", "https://api.bitwarden.eu" };
    case BitwardenRegion::Custom: {
        auto base = trim_trailing_slash(custom_base_url);
        return { base + "/identity", base + "/api" };
    }
    }
    return { "https://identity.bitwarden.com", "https://api.bitwarden.com" };
}

std::string BitwardenProtocol::device_type_code()
{
#if defined(__APPLE__)
    return "2"; // macOS Desktop
#else
    return "8"; // Linux Desktop
#endif
}

std::string BitwardenProtocol::url_encode(std::string_view input)
{
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size() * 3 / 2);
    for (unsigned char c : input) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(HEX[(c >> 4) & 0x0F]);
            out.push_back(HEX[c & 0x0F]);
        }
    }
    return out;
}

std::string BitwardenProtocol::json_escape(std::string_view input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char c : input) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

BitwardenHttpRequest BitwardenProtocol::build_prelogin_request(
    BitwardenEndpoints const& endpoints,
    std::string_view email)
{
    auto norm = BitwardenCrypto::normalize_email(email);
    BitwardenHttpRequest req;
    req.method = "POST";
    req.url = trim_trailing_slash(endpoints.identity_url) + "/accounts/prelogin";
    req.headers = {
        { "Content-Type", "application/json" },
        { "Accept", "application/json" },
        { "Bitwarden-Client-Name", std::string(CLIENT_NAME) },
        { "Bitwarden-Client-Version", std::string(CLIENT_VERSION) },
        { "Device-Type", device_type_code() },
    };
    req.body = "{\"email\":\"" + json_escape(norm) + "\"}";
    return req;
}

std::optional<BitwardenKdfConfig> BitwardenProtocol::parse_prelogin_response(std::string_view json)
{
    BwJsonParser parser(json);
    auto root = parser.parse();
    if (!root || !root->is_object())
        return std::nullopt;

    auto kdf_int = root->get_int("kdf");
    auto iterations = root->get_int("kdfIterations");
    if (!kdf_int.has_value() || !iterations.has_value() || *iterations <= 0)
        return std::nullopt;

    BitwardenKdfConfig cfg;
    if (*kdf_int == 0) {
        cfg.type = BitwardenKdfType::Pbkdf2Sha256;
        cfg.iterations = static_cast<uint32_t>(*iterations);
        return cfg;
    }
    if (*kdf_int == 1) {
        cfg.type = BitwardenKdfType::Argon2id;
        cfg.iterations = static_cast<uint32_t>(*iterations);
        cfg.memory_mb = static_cast<uint32_t>(root->get_int("kdfMemory").value_or(64));
        cfg.parallelism = static_cast<uint32_t>(root->get_int("kdfParallelism").value_or(4));
        return cfg;
    }
    return std::nullopt;
}

BitwardenHttpRequest BitwardenProtocol::build_password_token_request(
    BitwardenEndpoints const& endpoints,
    std::string_view email,
    std::string_view master_password_hash,
    std::string_view device_identifier,
    std::string_view device_name,
    std::optional<int> two_factor_provider,
    std::optional<std::string_view> two_factor_token,
    std::optional<std::string_view> new_device_otp)
{
    auto norm = BitwardenCrypto::normalize_email(email);
    BitwardenHttpRequest req;
    req.method = "POST";
    req.url = trim_trailing_slash(endpoints.identity_url) + "/connect/token";
    req.headers = {
        { "Content-Type", "application/x-www-form-urlencoded; charset=utf-8" },
        { "Accept", "application/json" },
        { "Bitwarden-Client-Name", std::string(CLIENT_NAME) },
        { "Bitwarden-Client-Version", std::string(CLIENT_VERSION) },
        { "Device-Type", device_type_code() },
        { "Auth-Email", BitwardenCrypto::auth_email_header(norm) },
    };

    std::string body;
    body += "grant_type=password";
    body += "&scope=api%20offline_access";
    body += "&client_id=" + url_encode(CLIENT_NAME);
    body += "&deviceType=" + url_encode(device_type_code());
    body += "&deviceIdentifier=" + url_encode(device_identifier);
    body += "&deviceName=" + url_encode(device_name);
    body += "&username=" + url_encode(norm);
    body += "&password=" + url_encode(master_password_hash);
    if (two_factor_provider.has_value() && two_factor_token.has_value()) {
        body += "&twoFactorProvider=" + std::to_string(*two_factor_provider);
        body += "&twoFactorToken=" + url_encode(*two_factor_token);
        body += "&twoFactorRemember=0";
    }
    if (new_device_otp.has_value() && !new_device_otp->empty()) {
        body += "&newDeviceOtp=" + url_encode(*new_device_otp);
    }
    req.body = std::move(body);
    return req;
}

BitwardenHttpRequest BitwardenProtocol::build_api_key_token_request(
    BitwardenEndpoints const& endpoints,
    std::string_view client_id,
    std::string_view client_secret,
    std::string_view device_identifier,
    std::string_view device_name)
{
    BitwardenHttpRequest req;
    req.method = "POST";
    req.url = trim_trailing_slash(endpoints.identity_url) + "/connect/token";
    req.headers = {
        { "Content-Type", "application/x-www-form-urlencoded; charset=utf-8" },
        { "Accept", "application/json" },
        { "Bitwarden-Client-Name", std::string(CLIENT_NAME) },
        { "Bitwarden-Client-Version", std::string(CLIENT_VERSION) },
        { "Device-Type", device_type_code() },
    };

    std::string body;
    body += "grant_type=client_credentials";
    body += "&scope=api";
    body += "&client_id=" + url_encode(client_id);
    body += "&client_secret=" + url_encode(client_secret);
    body += "&deviceType=" + url_encode(device_type_code());
    body += "&deviceIdentifier=" + url_encode(device_identifier);
    body += "&deviceName=" + url_encode(device_name);
    req.body = std::move(body);
    return req;
}

BitwardenHttpRequest BitwardenProtocol::build_refresh_token_request(
    BitwardenEndpoints const& endpoints,
    std::string_view refresh_token)
{
    BitwardenHttpRequest req;
    req.method = "POST";
    req.url = trim_trailing_slash(endpoints.identity_url) + "/connect/token";
    req.headers = {
        { "Content-Type", "application/x-www-form-urlencoded; charset=utf-8" },
        { "Accept", "application/json" },
        { "Bitwarden-Client-Name", std::string(CLIENT_NAME) },
        { "Bitwarden-Client-Version", std::string(CLIENT_VERSION) },
        { "Device-Type", device_type_code() },
    };
    req.body = "grant_type=refresh_token&client_id=" + url_encode(CLIENT_NAME) + "&refresh_token=" + url_encode(refresh_token);
    return req;
}

BitwardenTokenResponse BitwardenProtocol::parse_token_response(int http_status, std::string_view json)
{
    BitwardenTokenResponse res;
    BwJsonParser parser(json);
    auto root = parser.parse();
    if (!root || !root->is_object()) {
        res.error = "Invalid JSON response from Bitwarden identity server (HTTP " + std::to_string(http_status) + ")";
        return res;
    }

    if (http_status >= 200 && http_status < 300) {
        res.access_token = root->get_string("access_token");
        res.refresh_token = root->get_string("refresh_token");
        res.expires_in = root->get_int("expires_in").value_or(3600);
        res.protected_symmetric_key = root->get_string("Key");
        res.private_key = root->get_string("PrivateKey");
        if (auto kdf = root->get_int("Kdf")) {
            res.kdf.type = (*kdf == 1) ? BitwardenKdfType::Argon2id : BitwardenKdfType::Pbkdf2Sha256;
            res.kdf.iterations = static_cast<uint32_t>(root->get_int("KdfIterations").value_or(600000));
            res.kdf.memory_mb = static_cast<uint32_t>(root->get_int("KdfMemory").value_or(64));
            res.kdf.parallelism = static_cast<uint32_t>(root->get_int("KdfParallelism").value_or(4));
        }
        if (res.access_token.empty()) {
            res.error = "Token response missing access_token";
            return res;
        }
        res.ok = true;
        return res;
    }

    auto err = root->get_string("error");
    auto err_desc = root->get_string("error_description");
    if (auto const* err_model = root->get_ci("ErrorModel"); err_model && err_model->is_object()) {
        auto msg = err_model->get_string("Message");
        if (!msg.empty())
            err_desc = msg;
    }

    if (auto const* two_fa = root->get_ci("TwoFactorProviders2"); two_fa && two_fa->is_object()) {
        BitwardenTwoFactorChallenge challenge;
        challenge.message = err_desc.empty() ? "Two-step login required" : err_desc;
        for (auto const& [provider_key, _] : *two_fa->as_object()) {
            if (provider_key.empty())
                continue;
            char* end_ptr = nullptr;
            long val = std::strtol(provider_key.c_str(), &end_ptr, 10);
            if (end_ptr == provider_key.c_str() + provider_key.size())
                challenge.available_providers.push_back(static_cast<int>(val));
        }
        std::sort(challenge.available_providers.begin(), challenge.available_providers.end());
        res.challenge = std::move(challenge);
        res.error = "Two-step login required";
        return res;
    }

    auto err_desc_lower = err_desc;
    std::transform(err_desc_lower.begin(), err_desc_lower.end(), err_desc_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (err_desc_lower.find("new device verification") != std::string::npos || err == "device_error") {
        BitwardenTwoFactorChallenge challenge;
        challenge.is_new_device_verification = true;
        challenge.message = err_desc.empty() ? "New device verification required. Check your email for the code." : err_desc;
        res.challenge = std::move(challenge);
        res.error = challenge.message;
        return res;
    }

    res.error = !err_desc.empty() ? err_desc : (!err.empty() ? err : ("Authentication failed (HTTP " + std::to_string(http_status) + ")"));
    return res;
}

BitwardenHttpRequest BitwardenProtocol::build_sync_request(
    BitwardenEndpoints const& endpoints,
    std::string_view access_token)
{
    BitwardenHttpRequest req;
    req.method = "GET";
    req.url = trim_trailing_slash(endpoints.api_url) + "/sync?excludeDomains=true";
    req.headers = {
        { "Authorization", "Bearer " + std::string(access_token) },
        { "Accept", "application/json" },
        { "Bitwarden-Client-Name", std::string(CLIENT_NAME) },
        { "Bitwarden-Client-Version", std::string(CLIENT_VERSION) },
        { "Device-Type", device_type_code() },
    };
    return req;
}

BitwardenSyncResult BitwardenProtocol::parse_sync_response(int http_status, std::string_view json)
{
    BitwardenSyncResult res;
    if (http_status < 200 || http_status >= 300) {
        res.error = "Sync failed (HTTP " + std::to_string(http_status) + ")";
        return res;
    }

    BwJsonParser parser(json);
    auto root = parser.parse();
    if (!root || !root->is_object()) {
        res.error = "Invalid JSON in Bitwarden sync response";
        return res;
    }

    if (auto const* profile = root->get_ci("profile"); profile && profile->is_object()) {
        res.profile_email = profile->get_string("email");
        res.protected_symmetric_key = profile->get_string("key");
        res.encrypted_private_key = profile->get_string("privateKey");
        if (auto const* orgs = profile->get_ci("organizations"); orgs && orgs->is_array()) {
            for (auto const& org : *orgs->as_array()) {
                if (!org.is_object())
                    continue;
                auto oid = org.get_string("id");
                auto okey = org.get_string("key");
                if (!oid.empty() && !okey.empty())
                    res.encrypted_org_keys.emplace(std::move(oid), std::move(okey));
            }
        }
    }

    if (auto const* ciphers = root->get_ci("ciphers"); ciphers && ciphers->is_array()) {
        for (auto const& cnode : *ciphers->as_array()) {
            if (!cnode.is_object())
                continue;
            auto cipher = parse_cipher_node(cnode);
            if (!cipher.id.empty() && !cipher.deleted && cipher.type == 1)
                res.ciphers.push_back(std::move(cipher));
        }
    }

    res.ok = true;
    return res;
}

std::map<std::string, BitwardenSymmetricKey> BitwardenProtocol::unwrap_organization_keys(
    std::string_view encrypted_private_key,
    std::map<std::string, std::string> const& encrypted_org_keys,
    BitwardenSymmetricKey const& user_key)
{
    std::map<std::string, BitwardenSymmetricKey> out;
    if (encrypted_private_key.empty() || encrypted_org_keys.empty() || !user_key.is_valid())
        return out;

    auto pkcs8_der = BitwardenCrypto::decrypt_bytes(encrypted_private_key, user_key);
    if (!pkcs8_der)
        return out;

    for (auto const& [org_id, enc_key] : encrypted_org_keys) {
        auto unwrapped = BitwardenCrypto::rsa_decrypt(enc_key, pkcs8_der->span());
        if (!unwrapped)
            continue;
        auto sym = BitwardenSymmetricKey::from_bytes(unwrapped->span());
        if (sym)
            out.emplace(org_id, std::move(*sym));
    }
    return out;
}

std::optional<BitwardenDecryptedLogin> BitwardenProtocol::decrypt_cipher(
    BitwardenRawCipher const& cipher,
    BitwardenSymmetricKey const& user_key,
    std::map<std::string, BitwardenSymmetricKey> const& org_keys,
    std::string_view account_id)
{
    if (cipher.type != 1 || cipher.deleted)
        return std::nullopt;

    BitwardenSymmetricKey const* base_key = &user_key;
    if (!cipher.organization_id.empty()) {
        auto it = org_keys.find(cipher.organization_id);
        if (it == org_keys.end())
            return std::nullopt;
        base_key = &it->second;
    }

    BitwardenSymmetricKey per_cipher_key;
    BitwardenSymmetricKey const* active_key = base_key;
    if (!cipher.encrypted_key.empty()) {
        auto raw_ck = BitwardenCrypto::decrypt_bytes(cipher.encrypted_key, *base_key);
        if (!raw_ck)
            return std::nullopt;
        auto ck = BitwardenSymmetricKey::from_bytes(raw_ck->span());
        if (!ck)
            return std::nullopt;
        per_cipher_key = std::move(*ck);
        active_key = &per_cipher_key;
    }

    auto dec_opt = [&](std::string const& enc) -> std::string {
        if (enc.empty())
            return {};
        auto res = BitwardenCrypto::decrypt_string(enc, *active_key);
        return res.value_or(std::string {});
    };

    BitwardenDecryptedLogin out;
    out.organization_id = cipher.organization_id;
    out.summary.provider = ProviderKind::Bitwarden;
    out.summary.provider_account_id = std::string(account_id);
    out.summary.id = cipher.id;
    out.summary.name = dec_opt(cipher.encrypted_name);
    out.summary.username = dec_opt(cipher.encrypted_username);
    out.summary.favorite = cipher.favorite;

    out.secret.username = out.summary.username;
    out.secret.password = dec_opt(cipher.encrypted_password);
    out.summary.has_password = !out.secret.password.empty();

    if (!cipher.encrypted_totp.empty()) {
        auto totp = dec_opt(cipher.encrypted_totp);
        if (!totp.empty()) {
            out.summary.has_totp = true;
            out.secret.totp_uri = std::move(totp);
        }
    }
    if (!cipher.encrypted_notes.empty()) {
        auto notes = dec_opt(cipher.encrypted_notes);
        if (!notes.empty())
            out.secret.notes = std::move(notes);
    }

    for (auto const& u : cipher.uris) {
        auto dec_uri = dec_opt(u.encrypted_uri);
        if (!dec_uri.empty())
            out.summary.uris.push_back({ std::move(dec_uri), u.match_type });
    }

    return out;
}

std::optional<BitwardenHttpRequest> BitwardenProtocol::build_create_cipher_request(
    BitwardenEndpoints const& endpoints,
    std::string_view access_token,
    NewLogin const& login,
    BitwardenSymmetricKey const& user_key)
{
    auto body = build_encrypted_cipher_json(login, user_key);
    if (!body)
        return std::nullopt;

    BitwardenHttpRequest req;
    req.method = "POST";
    req.url = trim_trailing_slash(endpoints.api_url) + "/ciphers";
    req.headers = {
        { "Authorization", "Bearer " + std::string(access_token) },
        { "Content-Type", "application/json" },
        { "Accept", "application/json" },
        { "Bitwarden-Client-Name", std::string(CLIENT_NAME) },
        { "Bitwarden-Client-Version", std::string(CLIENT_VERSION) },
        { "Device-Type", device_type_code() },
    };
    req.body = std::move(*body);
    return req;
}

std::optional<BitwardenHttpRequest> BitwardenProtocol::build_update_cipher_request(
    BitwardenEndpoints const& endpoints,
    std::string_view access_token,
    std::string_view cipher_id,
    NewLogin const& full_login,
    BitwardenSymmetricKey const& user_key)
{
    if (cipher_id.empty())
        return std::nullopt;
    auto body = build_encrypted_cipher_json(full_login, user_key);
    if (!body)
        return std::nullopt;

    BitwardenHttpRequest req;
    req.method = "PUT";
    req.url = trim_trailing_slash(endpoints.api_url) + "/ciphers/" + url_encode(cipher_id);
    req.headers = {
        { "Authorization", "Bearer " + std::string(access_token) },
        { "Content-Type", "application/json" },
        { "Accept", "application/json" },
        { "Bitwarden-Client-Name", std::string(CLIENT_NAME) },
        { "Bitwarden-Client-Version", std::string(CLIENT_VERSION) },
        { "Device-Type", device_type_code() },
    };
    req.body = std::move(*body);
    return req;
}

std::optional<BitwardenRawCipher> BitwardenProtocol::parse_single_cipher_response(int http_status, std::string_view json)
{
    if (http_status < 200 || http_status >= 300)
        return std::nullopt;
    BwJsonParser parser(json);
    auto root = parser.parse();
    if (!root || !root->is_object())
        return std::nullopt;
    auto cipher = parse_cipher_node(*root);
    if (cipher.id.empty())
        return std::nullopt;
    return cipher;
}

}
