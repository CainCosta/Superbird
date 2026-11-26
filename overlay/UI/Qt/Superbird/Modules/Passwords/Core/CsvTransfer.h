/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>

#include <string>
#include <string_view>
#include <vector>

namespace Superbird::Passwords {

enum class CsvFormat : uint8_t {
    Unknown = 0,
    ApplePasswords = 1, // Title,URL,Username,Password,Notes,OTPAuth
    Chrome = 2,         // name,url,username,password[,note]
    Bitwarden = 3,      // folder,favorite,type,name,notes,fields,reprompt,login_uri,login_username,login_password,login_totp
    OnePassword = 4,    // Title,Url,Username,Password,OTPAuth,Notes or title,website,username,password,one-time password,notes
};

struct CsvImportResult {
    CsvFormat detected_format { CsvFormat::Unknown };
    std::vector<NewLogin> logins;
    size_t skipped_rows { 0 };
};

class CsvTransfer {
public:
    static std::vector<std::vector<std::string>> parse_csv_rows(std::string_view csv_text);
    static CsvFormat detect_format(std::vector<std::string> const& header_row);
    static CsvImportResult import_csv(std::string_view csv_text);

    // Exports logins to Apple Passwords / Safari or Bitwarden CSV format (RFC 4180).
    static std::string export_csv(std::vector<NewLogin> const& logins, CsvFormat format = CsvFormat::ApplePasswords);
};

}
