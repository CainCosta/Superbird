/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Superbird::Passwords {

enum class ProviderKind : uint8_t {
    Bitwarden = 0,
    LocalVault = 1,
    OnePassword = 2,
};

inline std::string_view provider_kind_name(ProviderKind kind)
{
    switch (kind) {
    case ProviderKind::Bitwarden:
        return "Bitwarden";
    case ProviderKind::LocalVault:
        return "Local Vault";
    case ProviderKind::OnePassword:
        return "1Password";
    }
    return "Unknown";
}

inline std::string_view provider_kind_id(ProviderKind kind)
{
    switch (kind) {
    case ProviderKind::Bitwarden:
        return "bitwarden";
    case ProviderKind::LocalVault:
        return "local_vault";
    case ProviderKind::OnePassword:
        return "onepassword";
    }
    return "unknown";
}

enum class ProviderState : uint8_t {
    Unconfigured = 0,
    Locked = 1,
    Unlocking = 2,
    Syncing = 3,
    Unlocked = 4,
    Error = 5,
};

enum class UriMatchType : uint8_t {
    BaseDomain = 0,
    Host = 1,
    StartsWith = 2,
    Exact = 3,
    RegularExpression = 4,
    Never = 5,
};

struct UriRule {
    std::string uri;
    UriMatchType match_type { UriMatchType::BaseDomain };
};

struct ProviderCapabilities {
    bool can_sync { false };
    bool can_create { false };
    bool can_update { false };
    bool supports_biometric_unlock { false };
    bool supports_totp { true };
};

struct CredentialSummary {
    ProviderKind provider { ProviderKind::LocalVault };
    std::string provider_account_id;
    std::string id;
    std::string name;
    std::string username;
    std::vector<UriRule> uris;
    bool has_password { false };
    bool has_totp { false };
    bool favorite { false };
};

struct CredentialSecret {
    std::string username;
    std::string password;
    std::optional<std::string> totp_uri;         // otpauth://... or raw Base32 secret
    std::optional<std::string> precomputed_totp; // used when provider computes TOTP (e.g., `op --otp`)
    std::optional<std::string> notes;
};

struct NewLogin {
    std::string name;
    std::string username;
    std::string password;
    std::vector<UriRule> uris;
    std::optional<std::string> totp_uri;
    std::optional<std::string> notes;
    bool favorite { false };
};

struct UpdatedLogin {
    std::optional<std::string> name;
    std::optional<std::string> username;
    std::optional<std::string> password;
    std::optional<std::vector<UriRule>> uris;
    std::optional<std::string> totp_uri;
    std::optional<std::string> notes;
    std::optional<bool> favorite;
};

enum class UnlockMethod : uint8_t {
    MasterPassword = 0,
    Biometric = 1,
    DesktopDelegated = 2,
};

struct UnlockRequest {
    UnlockMethod method { UnlockMethod::MasterPassword };
    std::string master_password;
};

enum class AutofillFieldKind : uint8_t {
    None = 0,
    Username = 1,
    Password = 2,
    NewPassword = 3,
    OneTimeCode = 4,
};

using StatusCallback = std::function<void(bool success, std::string error)>;
using SecretCallback = std::function<void(std::optional<CredentialSecret> secret, std::string error)>;
using WriteCallback = std::function<void(std::optional<CredentialSummary> saved, std::string error)>;

class PasswordProvider {
public:
    virtual ~PasswordProvider() = default;

    virtual ProviderKind kind() const = 0;
    virtual std::string account_id() const = 0;
    virtual std::string display_label() const = 0;
    virtual ProviderState status() const = 0;
    virtual ProviderCapabilities capabilities() const = 0;
    virtual std::string last_error() const = 0;
    virtual int64_t last_sync_time() const = 0;

    virtual void unlock(UnlockRequest const& request, StatusCallback callback) = 0;
    virtual void lock() = 0;
    virtual void sync(StatusCallback callback) = 0;

    virtual std::vector<CredentialSummary> match_for_url(std::string_view url) const = 0;
    virtual std::vector<CredentialSummary> search(std::string_view query) const = 0;
    virtual std::vector<CredentialSummary> all_credentials() const = 0;

    virtual void reveal_secret(std::string_view id, SecretCallback callback) = 0;
    virtual void create_login(NewLogin const& login, WriteCallback callback) = 0;
    virtual void update_login(std::string_view id, UpdatedLogin const& update, WriteCallback callback) = 0;
};

}
