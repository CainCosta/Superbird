/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenCrypto.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Superbird::Passwords {

enum class BitwardenRegion : uint8_t {
    US = 0,
    EU = 1,
    Custom = 2,
};

struct BitwardenEndpoints {
    std::string identity_url;
    std::string api_url;

    static BitwardenEndpoints for_region(BitwardenRegion region, std::string_view custom_base_url = {});
};

struct BitwardenTwoFactorChallenge {
    bool is_new_device_verification { false };
    std::vector<int> available_providers; // 0=Authenticator, 1=Email, 3=YubiKey
    std::string message;
};

struct BitwardenTokenResponse {
    bool ok { false };
    std::string access_token;
    std::string refresh_token;
    int64_t expires_in { 3600 };
    std::string protected_symmetric_key; // `Key`
    std::string private_key;             // `PrivateKey`
    BitwardenKdfConfig kdf;
    std::optional<BitwardenTwoFactorChallenge> challenge;
    std::string error;
};

struct BitwardenRawCipher {
    std::string id;
    std::string organization_id;
    int type { 1 }; // 1 = Login
    std::string encrypted_key;
    std::string encrypted_name;
    std::string encrypted_username;
    std::string encrypted_password;
    std::string encrypted_totp;
    std::string encrypted_notes;
    bool favorite { false };
    bool deleted { false };
    struct RawUri {
        std::string encrypted_uri;
        UriMatchType match_type { UriMatchType::BaseDomain };
    };
    std::vector<RawUri> uris;
    std::string raw_json;
};

struct BitwardenSyncResult {
    bool ok { false };
    std::string profile_email;
    std::string protected_symmetric_key;
    std::string encrypted_private_key;
    std::map<std::string, std::string> encrypted_org_keys; // org_id -> RSA-encrypted key
    std::vector<BitwardenRawCipher> ciphers;
    std::string error;
};

struct BitwardenDecryptedLogin {
    CredentialSummary summary;
    CredentialSecret secret;
    std::string organization_id;
};

struct BitwardenHttpRequest {
    std::string method; // GET, POST, PUT
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

class BitwardenProtocol {
public:
    static constexpr std::string_view CLIENT_NAME = "desktop";
    static constexpr std::string_view CLIENT_VERSION = "2026.1.0";

    static std::string device_type_code();
    static std::string url_encode(std::string_view input);
    static std::string json_escape(std::string_view input);

    static BitwardenHttpRequest build_prelogin_request(
        BitwardenEndpoints const& endpoints,
        std::string_view email);

    static std::optional<BitwardenKdfConfig> parse_prelogin_response(std::string_view json);

    static BitwardenHttpRequest build_password_token_request(
        BitwardenEndpoints const& endpoints,
        std::string_view email,
        std::string_view master_password_hash,
        std::string_view device_identifier,
        std::string_view device_name,
        std::optional<int> two_factor_provider = std::nullopt,
        std::optional<std::string_view> two_factor_token = std::nullopt,
        std::optional<std::string_view> new_device_otp = std::nullopt);

    static BitwardenHttpRequest build_api_key_token_request(
        BitwardenEndpoints const& endpoints,
        std::string_view client_id,
        std::string_view client_secret,
        std::string_view device_identifier,
        std::string_view device_name);

    static BitwardenHttpRequest build_refresh_token_request(
        BitwardenEndpoints const& endpoints,
        std::string_view refresh_token);

    static BitwardenTokenResponse parse_token_response(int http_status, std::string_view json);

    static BitwardenHttpRequest build_sync_request(
        BitwardenEndpoints const& endpoints,
        std::string_view access_token);

    static BitwardenSyncResult parse_sync_response(int http_status, std::string_view json);

    static std::optional<BitwardenDecryptedLogin> decrypt_cipher(
        BitwardenRawCipher const& cipher,
        BitwardenSymmetricKey const& user_key,
        std::map<std::string, BitwardenSymmetricKey> const& org_keys,
        std::string_view account_id);

    static std::map<std::string, BitwardenSymmetricKey> unwrap_organization_keys(
        std::string_view encrypted_private_key,
        std::map<std::string, std::string> const& encrypted_org_keys,
        BitwardenSymmetricKey const& user_key);

    static std::optional<BitwardenHttpRequest> build_create_cipher_request(
        BitwardenEndpoints const& endpoints,
        std::string_view access_token,
        NewLogin const& login,
        BitwardenSymmetricKey const& user_key);

    static std::optional<BitwardenHttpRequest> build_update_cipher_request(
        BitwardenEndpoints const& endpoints,
        std::string_view access_token,
        std::string_view cipher_id,
        NewLogin const& full_login,
        BitwardenSymmetricKey const& user_key);

    static std::optional<BitwardenRawCipher> parse_single_cipher_response(int http_status, std::string_view json);
};

}
