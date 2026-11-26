/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>

#include <QObject>
#include <QString>

#include <functional>
#include <string>
#include <vector>

namespace Superbird::Passwords {

// Official 1Password CLI v2 (`op`) bridge using the running 1Password desktop app (Touch ID / Polkit).
// Zero 1Password secrets are ever persisted in Superbird; only non-secret item metadata is cached in memory.
class OnePasswordProvider final
    : public QObject
    , public PasswordProvider {
    Q_OBJECT

public:
    explicit OnePasswordProvider(QObject* parent = nullptr);
    virtual ~OnePasswordProvider() override;

    // ^PasswordProvider
    virtual ProviderKind kind() const override { return ProviderKind::OnePassword; }
    virtual std::string account_id() const override { return "1password"; }
    virtual std::string display_label() const override { return "1Password"; }
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

    static QString find_op_binary();
    static bool is_cli_available();
    static std::string setup_guidance();

    void fetch_otp(std::string_view id, std::function<void(std::optional<std::string> otp, std::string error)> callback);

    std::function<void()> on_changed;

private:
    void run_op_async(
        QStringList const& args,
        QByteArray const& stdin_data,
        std::function<void(int exit_code, std::string stdout_str, std::string stderr_str)> callback);

    ProviderState m_state { ProviderState::Unconfigured };
    std::string m_last_error;
    int64_t m_last_sync_time { 0 };
    std::vector<CredentialSummary> m_cached_summaries;
};

}
