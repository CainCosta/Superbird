/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibURL/Host.h>
#include <LibURL/Parser.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordsActions.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/UriMatcher.h>
#include <UI/Qt/Superbird/Modules/Passwords/PasswordManagerService.h>
#include <UI/Qt/Superbird/Modules/Passwords/Storage/PasswordsStore.h>

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>

#include <openssl/sha.h>

namespace Superbird::Passwords {

PasswordManagerService& PasswordManagerService::the()
{
    static PasswordManagerService* instance = nullptr;
    if (!instance)
        instance = new PasswordManagerService(QCoreApplication::instance());
    return *instance;
}

PasswordManagerService::PasswordManagerService(QObject* parent)
    : QObject(parent)
    , m_auto_lock_timer(new QTimer(this))
    , m_clipboard_clear_timer(new QTimer(this))
{
    // Wire LibURL's libpsl-backed registrable_domain() into the Qt-free UriMatcher.
    UriMatcher::set_registrable_domain_resolver([](std::string_view host) -> std::optional<std::string> {
        auto parsed = URL::Parser::basic_parse(MUST(String::formatted("https://{}", StringView { host.data(), host.size() })));
        if (!parsed.has_value() || !parsed->host().has_value())
            return std::nullopt;
        auto reg = parsed->host()->registrable_domain();
        if (!reg.has_value())
            return std::nullopt;
        auto sv = reg->bytes_as_string_view();
        return std::string(sv.characters_without_null_termination(), sv.length());
    });

    auto& store = PasswordsStore::the();
    m_local_vault = new LocalVaultProvider(store, this);
    m_bitwarden = new BitwardenProvider(store, this);
    m_one_password = new OnePasswordProvider(this);

    auto notify = [this] { emit stateChanged(); };
    m_local_vault->on_changed = notify;
    m_bitwarden->on_changed = notify;
    m_one_password->on_changed = notify;

    m_auto_lock_timer->setSingleShot(true);
    connect(m_auto_lock_timer, &QTimer::timeout, this, [this] {
        lock_all();
    });

    m_clipboard_clear_timer->setSingleShot(true);
    connect(m_clipboard_clear_timer, &QTimer::timeout, this, [this] {
        if (!m_copied_secret_hash.has_value())
            return;
        auto* clipboard = QApplication::clipboard();
        if (clipboard) {
            auto current = clipboard->text().toUtf8();
            auto current_hash = sha256_bytes({ current.constData(), static_cast<size_t>(current.size()) });
            if (constant_time_equals(current_hash, *m_copied_secret_hash))
                clipboard->clear();
        }
        m_copied_secret_hash.reset();
    });

    if (auto* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, [this] {
            lock_all();
        });
    }

    refresh_settings();
}

PasswordManagerService::~PasswordManagerService()
{
    lock_all();
}

PasswordProvider* PasswordManagerService::provider(ProviderKind kind)
{
    switch (kind) {
    case ProviderKind::Bitwarden:
        return m_bitwarden;
    case ProviderKind::LocalVault:
        return m_local_vault;
    case ProviderKind::OnePassword:
        return m_one_password;
    }
    return nullptr;
}

bool PasswordManagerService::is_provider_enabled(ProviderKind kind) const
{
    switch (kind) {
    case ProviderKind::Bitwarden:
        return WindowHost::module_option_is_on(PASSWORDS_MODULE_ID, "provider_bitwarden"sv);
    case ProviderKind::LocalVault:
        return WindowHost::module_option_is_on(PASSWORDS_MODULE_ID, "provider_local_vault"sv);
    case ProviderKind::OnePassword:
        return WindowHost::module_option_is_on(PASSWORDS_MODULE_ID, "provider_onepassword"sv);
    }
    return false;
}

std::vector<PasswordProvider*> PasswordManagerService::enabled_providers() const
{
    std::vector<PasswordProvider*> out;
    if (is_provider_enabled(ProviderKind::Bitwarden))
        out.push_back(m_bitwarden);
    if (is_provider_enabled(ProviderKind::LocalVault))
        out.push_back(m_local_vault);
    if (is_provider_enabled(ProviderKind::OnePassword))
        out.push_back(m_one_password);
    return out;
}

std::vector<PasswordProvider*> PasswordManagerService::unlocked_providers() const
{
    std::vector<PasswordProvider*> out;
    for (auto* p : enabled_providers()) {
        if (p->status() == ProviderState::Unlocked)
            out.push_back(p);
    }
    return out;
}

bool PasswordManagerService::any_provider_unlocked() const
{
    return !unlocked_providers().empty();
}

bool PasswordManagerService::any_provider_configured() const
{
    for (auto* p : enabled_providers()) {
        if (p->status() != ProviderState::Unconfigured)
            return true;
    }
    return false;
}

std::vector<CredentialSummary> PasswordManagerService::match_for_url(std::string_view url) const
{
    std::vector<CredentialSummary> all;
    for (auto* p : unlocked_providers()) {
        auto creds = p->all_credentials();
        all.insert(all.end(), std::make_move_iterator(creds.begin()), std::make_move_iterator(creds.end()));
    }
    return UriMatcher::filter_and_rank(url, all);
}

std::vector<CredentialSummary> PasswordManagerService::search(std::string_view query) const
{
    std::vector<CredentialSummary> all;
    for (auto* p : unlocked_providers()) {
        auto creds = p->all_credentials();
        all.insert(all.end(), std::make_move_iterator(creds.begin()), std::make_move_iterator(creds.end()));
    }
    return UriMatcher::search(query, all);
}

void PasswordManagerService::reveal_secret(CredentialSummary const& summary, SecretCallback callback)
{
    touch_activity();
    auto* p = provider(summary.provider);
    if (!p) {
        if (callback)
            callback(std::nullopt, "Unknown password provider.");
        return;
    }
    p->reveal_secret(summary.id, std::move(callback));
}

void PasswordManagerService::create_login(ProviderKind kind, NewLogin const& login, WriteCallback callback)
{
    touch_activity();
    auto* p = provider(kind);
    if (!p) {
        if (callback)
            callback(std::nullopt, "Unknown password provider.");
        return;
    }
    p->create_login(login, std::move(callback));
}

void PasswordManagerService::update_login(CredentialSummary const& summary, UpdatedLogin const& update, WriteCallback callback)
{
    touch_activity();
    auto* p = provider(summary.provider);
    if (!p) {
        if (callback)
            callback(std::nullopt, "Unknown password provider.");
        return;
    }
    p->update_login(summary.id, update, std::move(callback));
}

void PasswordManagerService::sync_all()
{
    touch_activity();
    for (auto* p : unlocked_providers()) {
        if (p->capabilities().can_sync)
            p->sync({});
    }
}

void PasswordManagerService::lock_all()
{
    m_auto_lock_timer->stop();
    m_local_vault->lock();
    m_bitwarden->lock();
    m_one_password->lock();
    emit stateChanged();
}

void PasswordManagerService::refresh_settings()
{
    auto timeout = WindowHost::module_option(PASSWORDS_MODULE_ID, "auto_lock_timeout"sv);
    if (timeout == "5m"sv)
        m_auto_lock_minutes = 5;
    else if (timeout == "15m"sv)
        m_auto_lock_minutes = 15;
    else if (timeout == "60m"sv)
        m_auto_lock_minutes = 60;
    else
        m_auto_lock_minutes = 0; // On quit

    if (!is_provider_enabled(ProviderKind::LocalVault) && m_local_vault->status() == ProviderState::Unlocked)
        m_local_vault->lock();
    if (!is_provider_enabled(ProviderKind::Bitwarden) && m_bitwarden->status() == ProviderState::Unlocked)
        m_bitwarden->lock();
    if (!is_provider_enabled(ProviderKind::OnePassword) && m_one_password->status() == ProviderState::Unlocked)
        m_one_password->lock();

    touch_activity();
    emit stateChanged();
}

void PasswordManagerService::touch_activity()
{
    if (m_auto_lock_minutes <= 0 || !any_provider_unlocked()) {
        m_auto_lock_timer->stop();
        return;
    }
    m_auto_lock_timer->start(m_auto_lock_minutes * 60 * 1000);
}

std::array<uint8_t, 32> PasswordManagerService::sha256_bytes(std::string_view data)
{
    std::array<uint8_t, 32> hash {};
    SHA256(reinterpret_cast<unsigned char const*>(data.data()), data.size(), hash.data());
    return hash;
}

void PasswordManagerService::copy_secret_to_clipboard(std::string_view secret, int clear_after_ms)
{
    touch_activity();
    auto* clipboard = QApplication::clipboard();
    if (!clipboard)
        return;
    clipboard->setText(QString::fromUtf8(secret.data(), static_cast<qsizetype>(secret.size())));
    m_copied_secret_hash = sha256_bytes(secret);
    m_clipboard_clear_timer->start(clear_after_ms);
}

void PasswordManagerService::copy_plain_to_clipboard(std::string_view text)
{
    touch_activity();
    auto* clipboard = QApplication::clipboard();
    if (!clipboard)
        return;
    clipboard->setText(QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())));
    m_copied_secret_hash.reset();
    m_clipboard_clear_timer->stop();
}

bool PasswordManagerService::is_never_save_host(std::string_view host) const
{
    return PasswordsStore::the().is_never_save_host(host);
}

void PasswordManagerService::add_never_save_host(std::string_view host)
{
    PasswordsStore::the().add_never_save_host(host);
}

void PasswordManagerService::classify_submitted_credential(
    std::string_view origin_url,
    std::string_view username,
    std::string_view password,
    std::function<void(SaveMatchInfo)> callback)
{
    if (!callback)
        return;

    auto matches = match_for_url(origin_url);
    for (auto const& match : matches) {
        if (!username.empty() && match.username == username) {
            // Only synchronously compare in-memory providers (LocalVault & Bitwarden); avoid spawning `op` on submit.
            if (match.provider == ProviderKind::OnePassword) {
                callback({ SaveMatchInfo::Kind::UpdateExisting, match });
                return;
            }
            std::string pw_copy(password);
            reveal_secret(match, [match, pw_copy, callback](std::optional<CredentialSecret> secret, std::string const&) mutable {
                if (secret && secret->password == pw_copy) {
                    cleanse_string(pw_copy);
                    callback({ SaveMatchInfo::Kind::AlreadyIdentical, match });
                    return;
                }
                cleanse_string(pw_copy);
                callback({ SaveMatchInfo::Kind::UpdateExisting, match });
            });
            return;
        }
    }

    callback({ SaveMatchInfo::Kind::NewLogin, std::nullopt });
}

}
