/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>
#include <UI/Qt/Superbird/Modules/Passwords/Providers/BitwardenProvider.h>
#include <UI/Qt/Superbird/Modules/Passwords/Providers/LocalVaultProvider.h>
#include <UI/Qt/Superbird/Modules/Passwords/Providers/OnePasswordProvider.h>

#include <QObject>
#include <QTimer>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace Superbird::Passwords {

// App-wide coordinator owning all enabled PasswordProviders, the idle auto-lock timer,
// and the 30-second secret clipboard clear timer.
class PasswordManagerService final : public QObject {
    Q_OBJECT

public:
    static PasswordManagerService& the();

    LocalVaultProvider& local_vault() { return *m_local_vault; }
    BitwardenProvider& bitwarden() { return *m_bitwarden; }
    OnePasswordProvider& one_password() { return *m_one_password; }

    PasswordProvider* provider(ProviderKind kind);
    std::vector<PasswordProvider*> enabled_providers() const;
    std::vector<PasswordProvider*> unlocked_providers() const;
    bool is_provider_enabled(ProviderKind kind) const;
    bool any_provider_unlocked() const;
    bool any_provider_configured() const;

    // Aggregated queries across all enabled + unlocked providers.
    std::vector<CredentialSummary> match_for_url(std::string_view url) const;
    std::vector<CredentialSummary> search(std::string_view query) const;

    void reveal_secret(CredentialSummary const& summary, SecretCallback callback);
    void create_login(ProviderKind kind, NewLogin const& login, WriteCallback callback);
    void update_login(CredentialSummary const& summary, UpdatedLogin const& update, WriteCallback callback);

    void sync_all();
    void lock_all();

    // Auto-lock idle timer & settings refresh.
    void refresh_settings();
    void touch_activity();

    // Copies a sensitive value (password or TOTP) to the system clipboard and schedules
    // an automatic clear after `clear_after_ms` (default 30s) if the clipboard still holds it.
    void copy_secret_to_clipboard(std::string_view secret, int clear_after_ms = 30000);
    void copy_plain_to_clipboard(std::string_view text);

    // "Never for this site" helpers.
    bool is_never_save_host(std::string_view host) const;
    void add_never_save_host(std::string_view host);

    // Checks if a submitted (origin, username, password) already exists with identical or different password.
    struct SaveMatchInfo {
        enum class Kind : uint8_t {
            NewLogin,
            UpdateExisting,
            AlreadyIdentical,
        };
        Kind kind { Kind::NewLogin };
        std::optional<CredentialSummary> existing;
    };
    void classify_submitted_credential(
        std::string_view origin_url,
        std::string_view username,
        std::string_view password,
        std::function<void(SaveMatchInfo)> callback);

signals:
    void stateChanged();

private:
    explicit PasswordManagerService(QObject* parent = nullptr);
    virtual ~PasswordManagerService() override;

    static std::array<uint8_t, 32> sha256_bytes(std::string_view data);

    LocalVaultProvider* m_local_vault { nullptr };
    BitwardenProvider* m_bitwarden { nullptr };
    OnePasswordProvider* m_one_password { nullptr };

    QTimer* m_auto_lock_timer { nullptr };
    QTimer* m_clipboard_clear_timer { nullptr };
    std::optional<std::array<uint8_t, 32>> m_copied_secret_hash;
    int m_auto_lock_minutes { 15 };
};

}
