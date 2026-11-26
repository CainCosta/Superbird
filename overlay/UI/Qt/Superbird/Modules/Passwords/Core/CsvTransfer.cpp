/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/CsvTransfer.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace Superbird::Passwords {

namespace {

std::string normalize_header(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c == ' ' || c == '_' || c == '-' || c == '\r' || c == '\n' || c == '\t')
            continue;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string escape_csv_field(std::string_view field)
{
    bool needs_quotes = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes)
        return std::string(field);
    std::string out;
    out.reserve(field.size() + 4);
    out.push_back('"');
    for (char c : field) {
        if (c == '"')
            out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string trim_copy(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return std::string(s);
}

} // namespace

std::vector<std::vector<std::string>> CsvTransfer::parse_csv_rows(std::string_view csv_text)
{
    // Strip UTF-8 BOM if present.
    if (csv_text.size() >= 3
        && static_cast<unsigned char>(csv_text[0]) == 0xEF
        && static_cast<unsigned char>(csv_text[1]) == 0xBB
        && static_cast<unsigned char>(csv_text[2]) == 0xBF) {
        csv_text.remove_prefix(3);
    }

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> current_row;
    std::string current_field;
    bool in_quotes = false;
    bool field_started = false;

    for (size_t i = 0; i < csv_text.size(); ++i) {
        char c = csv_text[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < csv_text.size() && csv_text[i + 1] == '"') {
                    current_field.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                current_field.push_back(c);
            }
            continue;
        }

        if (!field_started && c == '"') {
            in_quotes = true;
            field_started = true;
        } else if (c == ',') {
            current_row.push_back(std::move(current_field));
            current_field.clear();
            field_started = false;
        } else if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < csv_text.size() && csv_text[i + 1] == '\n')
                ++i;
            current_row.push_back(std::move(current_field));
            current_field.clear();
            field_started = false;
            bool all_empty = std::all_of(current_row.begin(), current_row.end(), [](std::string const& s) { return s.empty(); });
            if (!all_empty)
                rows.push_back(std::move(current_row));
            current_row.clear();
        } else {
            current_field.push_back(c);
            field_started = true;
        }
    }

    if (field_started || !current_row.empty()) {
        current_row.push_back(std::move(current_field));
        bool all_empty = std::all_of(current_row.begin(), current_row.end(), [](std::string const& s) { return s.empty(); });
        if (!all_empty)
            rows.push_back(std::move(current_row));
    }

    return rows;
}

CsvFormat CsvTransfer::detect_format(std::vector<std::string> const& header_row)
{
    std::unordered_map<std::string, size_t> cols;
    for (size_t i = 0; i < header_row.size(); ++i)
        cols[normalize_header(header_row[i])] = i;

    if (cols.contains("loginuri") && cols.contains("loginusername") && cols.contains("loginpassword"))
        return CsvFormat::Bitwarden;
    if (cols.contains("title") && cols.contains("url") && cols.contains("username") && cols.contains("password") && cols.contains("otpauth")) {
        // Distinguish Apple Passwords (`Title,URL,Username,Password,Notes,OTPAuth`) vs 1Password (`Title,Url,Username,Password,OTPAuth,Notes`).
        if (header_row.size() >= 6 && header_row[1] == "URL")
            return CsvFormat::ApplePasswords;
        if (cols.contains("otpauth") && cols.contains("notes") && cols["otpauth"] < cols["notes"])
            return CsvFormat::OnePassword;
        return CsvFormat::ApplePasswords;
    }
    if (cols.contains("title") && (cols.contains("website") || cols.contains("onetimepassword")))
        return CsvFormat::OnePassword;
    if (cols.contains("name") && cols.contains("url") && cols.contains("username") && cols.contains("password"))
        return CsvFormat::Chrome;
    if (cols.contains("title") && cols.contains("url") && cols.contains("username") && cols.contains("password"))
        return CsvFormat::ApplePasswords;
    return CsvFormat::Unknown;
}

CsvImportResult CsvTransfer::import_csv(std::string_view csv_text)
{
    CsvImportResult result;
    auto rows = parse_csv_rows(csv_text);
    if (rows.size() < 2)
        return result;

    result.detected_format = detect_format(rows[0]);
    if (result.detected_format == CsvFormat::Unknown)
        return result;

    std::unordered_map<std::string, size_t> cols;
    for (size_t i = 0; i < rows[0].size(); ++i)
        cols[normalize_header(rows[0][i])] = i;

    auto get = [&](std::vector<std::string> const& row, std::initializer_list<char const*> keys) -> std::string {
        for (char const* k : keys) {
            auto it = cols.find(k);
            if (it != cols.end() && it->second < row.size())
                return row[it->second];
        }
        return {};
    };

    for (size_t r = 1; r < rows.size(); ++r) {
        auto const& row = rows[r];

        if (result.detected_format == CsvFormat::Bitwarden) {
            auto type = normalize_header(get(row, { "type" }));
            if (!type.empty() && type != "login") {
                ++result.skipped_rows;
                continue;
            }
        }

        NewLogin login;
        login.name = trim_copy(get(row, { "title", "name" }));
        login.username = trim_copy(get(row, { "loginusername", "username" }));
        login.password = get(row, { "loginpassword", "password" });

        auto url_field = trim_copy(get(row, { "loginuri", "url", "website" }));
        if (!url_field.empty()) {
            // Bitwarden exports multiple URIs separated by commas or newlines inside login_uri.
            size_t pos = 0;
            while (pos < url_field.size()) {
                auto sep = url_field.find_first_of(",\n", pos);
                auto piece = (sep == std::string::npos) ? url_field.substr(pos) : url_field.substr(pos, sep - pos);
                auto cleaned = trim_copy(piece);
                if (!cleaned.empty())
                    login.uris.push_back({ std::move(cleaned), UriMatchType::BaseDomain });
                if (sep == std::string::npos)
                    break;
                pos = sep + 1;
            }
        }

        auto totp = trim_copy(get(row, { "logintotp", "otpauth", "onetimepassword" }));
        if (!totp.empty())
            login.totp_uri = std::move(totp);

        auto notes = get(row, { "notes", "note" });
        if (!notes.empty())
            login.notes = std::move(notes);

        auto fav = normalize_header(get(row, { "favorite" }));
        login.favorite = (fav == "1" || fav == "true" || fav == "yes");

        if (login.name.empty() && !login.uris.empty())
            login.name = login.uris[0].uri;

        if (login.name.empty() && login.username.empty() && login.password.empty() && login.uris.empty()) {
            ++result.skipped_rows;
            continue;
        }

        result.logins.push_back(std::move(login));
    }

    return result;
}

std::string CsvTransfer::export_csv(std::vector<NewLogin> const& logins, CsvFormat format)
{
    std::string out;
    if (format == CsvFormat::Bitwarden) {
        out += "folder,favorite,type,name,notes,fields,reprompt,login_uri,login_username,login_password,login_totp\n";
        for (auto const& l : logins) {
            std::string uris;
            for (size_t i = 0; i < l.uris.size(); ++i) {
                if (i > 0)
                    uris.push_back(',');
                uris += l.uris[i].uri;
            }
            out += ",";
            out += l.favorite ? "1" : "";
            out += ",login,";
            out += escape_csv_field(l.name);
            out += ",";
            out += escape_csv_field(l.notes.value_or(""));
            out += ",,0,";
            out += escape_csv_field(uris);
            out += ",";
            out += escape_csv_field(l.username);
            out += ",";
            out += escape_csv_field(l.password);
            out += ",";
            out += escape_csv_field(l.totp_uri.value_or(""));
            out += "\n";
        }
        return out;
    }

    // Default: Apple Passwords / Safari CSV format.
    out += "Title,URL,Username,Password,Notes,OTPAuth\n";
    for (auto const& l : logins) {
        std::string url = l.uris.empty() ? std::string {} : l.uris[0].uri;
        out += escape_csv_field(l.name);
        out += ",";
        out += escape_csv_field(url);
        out += ",";
        out += escape_csv_field(l.username);
        out += ",";
        out += escape_csv_field(l.password);
        out += ",";
        out += escape_csv_field(l.notes.value_or(""));
        out += ",";
        out += escape_csv_field(l.totp_uri.value_or(""));
        out += "\n";
    }
    return out;
}

}
