/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/CsvTransfer.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/LocalVaultCrypto.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>
#include <UI/Qt/Superbird/Modules/Passwords/Storage/PasswordsStore.h>

#include <QObject>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Superbird::Passwords {

class LocalVaultProvider final
    : public QObject
    , public PasswordProvider {
    Q_OBJECT

public:
    explicit LocalVaultProvider(PasswordsStore& store, QObject* parent = nullptr);
    virtual ~LocalVaultProvider() override;

    // ^PasswordProvider
    virtual ProviderKind kind() const override { return ProviderKind::LocalVault; }
    virtual std::string account_id() const override { return "local"; }
    virtual std::string display_label() const override { return "Local Vault"; }
    virtual ProviderState status() const override { return m_state; }
    virtual ProviderCapabilities capabilities() const override;
    virtual std::string last_error() const override { return m_last_error; }
    virtual int64_t last_sync_time() const override { return 0; }

    virtual void unlock(UnlockRequest const& request, StatusCallback callback) override;
    virtual void lock() override;
    virtual void sync(StatusCallback callback) override;

    virtual std::vector<CredentialSummary> match_for_url(std::string_view url) const override;
    virtual std::vector<CredentialSummary> search(std::string_view query) const override;
    virtual std::vector<CredentialSummary> all_credentials() const override;

    virtual void reveal_secret(std::string_view id, SecretCallback callback) override;
    virtual void create_login(NewLogin const& login, WriteCallback callback) override;
    virtual void update_login(std::string_view id, UpdatedLogin const& update, WriteCallback callback) override;

    // Local Vault management
    bool is_configured() const;
    bool has_biometric_unlock() const;
    bool setup_vault(std::string_view master_passphrase, bool enable_biometric, std::string& error_out);
    bool enable_biometric_unlock(bool enable);
    bool delete_login(std::string_view id);
    bool reset_vault();

    CsvImportResult import_csv(std::string_view csv_text, size_t& imported_count);
    std::optional<std::string> export_csv(CsvFormat format = CsvFormat::ApplePasswords) const;

    std::function<void()> on_changed;

private:
    struct DecryptedItem {
        CredentialSummary summary;
        CredentialSecret secret;
        int64_t created_at { 0 };
        int64_t updated_at { 0 };
        std::vector<std::string> password_history;
    };

    bool load_and_decrypt_all_items();
    bool persist_item(DecryptedItem const& item);
    void clear_decrypted_memory();
    static std::string serialize_item_json(DecryptedItem const& item);
    static std::optional<DecryptedItem> parse_item_json(std::string_view id, int64_t created_at, int64_t updated_at, std::string_view json);
    static std::string generate_item_id();

    PasswordsStore& m_store;
    ProviderState m_state { ProviderState::Unconfigured };
    std::string m_last_error;
    SecureBytes m_dek;
    std::map<std::string, DecryptedItem> m_items;
};

}
