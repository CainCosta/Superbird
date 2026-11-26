/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenCrypto.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenProtocol.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>
#include <UI/Qt/Superbird/Modules/Passwords/Storage/PasswordsStore.h>

#include <QObject>
#include <QTimer>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Superbird::Passwords {

struct BitwardenLoginResult {
    bool ok { false };
    std::optional<BitwardenTwoFactorChallenge> challenge;
    std::string error;
};

using BitwardenLoginCallback = std::function<void(BitwardenLoginResult)>;

class BitwardenProvider final
    : public QObject
    , public PasswordProvider {
    Q_OBJECT

public:
    explicit BitwardenProvider(PasswordsStore& store, QObject* parent = nullptr);
    virtual ~BitwardenProvider() override;

    // ^PasswordProvider
    virtual ProviderKind kind() const override { return ProviderKind::Bitwarden; }
    virtual std::string account_id() const override { return m_email; }
    virtual std::string display_label() const override;
    virtual ProviderState status() const override { return m_state; }
    virtual ProviderCapabilities capabilities() const override;
    virtual std::string last_error() const override { return m_last_error; }
    virtual int64_t last_sync_time() const override { return m_last_sync_time; }

    virtual void unlock(UnlockRequest const& request, StatusCallback callback) override;
    virtual void lock() override;
    virtual void sync(StatusCallback callback) override;

    virtual std::vector<CredentialSummary> match_for_url(std::string_view url) const override;
    virtual std::vector<CredentialSummary> search(std::string_view query) const override;
    virtual std::vector<CredentialSummary> all_credentials() const override;

    virtual void reveal_secret(std::string_view id, SecretCallback callback) override;
    virtual void create_login(NewLogin const& login, WriteCallback callback) override;
    virtual void update_login(std::string_view id, UpdatedLogin const& update, WriteCallback callback) override;

    // Bitwarden-specific authentication & account config
    bool is_configured() const;
    bool has_biometric_unlock() const;
    BitwardenRegion region() const { return m_region; }
    std::string const& custom_server_url() const { return m_custom_server_url; }
    std::string const& email() const { return m_email; }

    void login_with_password(
        BitwardenRegion region,
        std::string_view custom_server_url,
        std::string_view email,
        std::string_view master_password,
        std::optional<int> two_factor_provider,
        std::optional<std::string_view> two_factor_token,
        std::optional<std::string_view> new_device_otp,
        bool enable_biometric,
        BitwardenLoginCallback callback);

    void login_with_api_key(
        BitwardenRegion region,
        std::string_view custom_server_url,
        std::string_view email,
        std::string_view client_id,
        std::string_view client_secret,
        std::string_view master_password,
        bool enable_biometric,
        BitwardenLoginCallback callback);

    void sign_out();

    std::function<void()> on_changed;

private:
    void load_persisted_account_config();
    void save_persisted_account_config();
    std::string ensure_device_identifier();
    void send_http(BitwardenHttpRequest const& req, std::function<void(int status, std::string body, std::string net_error)> callback);
    void ensure_valid_access_token(std::function<void(bool ok, std::string error)> callback);
    void decrypt_cached_ciphers();
    void apply_sync_result(BitwardenSyncResult const& sync_result);
    void clear_decrypted_memory();
    void save_tokens_encrypted();
    void load_tokens_encrypted();
    static std::string serialize_raw_cipher(BitwardenRawCipher const& c);
    static std::optional<BitwardenRawCipher> deserialize_raw_cipher(std::string_view json);

    PasswordsStore& m_store;
    QTimer* m_periodic_sync_timer { nullptr };

    ProviderState m_state { ProviderState::Unconfigured };
    std::string m_last_error;
    int64_t m_last_sync_time { 0 };

    BitwardenRegion m_region { BitwardenRegion::US };
    std::string m_custom_server_url;
    std::string m_email;
    std::string m_device_identifier;
    BitwardenKdfConfig m_kdf;
    std::string m_protected_symmetric_key;
    std::string m_encrypted_private_key;
    std::map<std::string, std::string> m_encrypted_org_keys;
    bool m_biometric_enabled { false };

    // Runtime secrets (cleansed on lock / destroy)
    BitwardenSymmetricKey m_user_key;
    std::map<std::string, BitwardenSymmetricKey> m_org_keys;
    std::string m_access_token;
    std::string m_refresh_token;
    std::string m_api_client_id;
    std::string m_api_client_secret;
    int64_t m_access_token_expiry { 0 };

    std::map<std::string, BitwardenDecryptedLogin> m_logins;
};

}
