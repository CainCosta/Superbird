/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/UriMatcher.h>
#include <UI/Qt/Superbird/Modules/Passwords/Keyring/Keyring.h>
#include <UI/Qt/Superbird/Modules/Passwords/Providers/LocalVaultProvider.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <cstring>

namespace Superbird::Passwords {

namespace {

constexpr std::string_view STORE_NS = "local_vault";
constexpr std::string_view KEYRING_SERVICE = "org.superbird.Superbird.LocalVault";
constexpr std::string_view KEYRING_ACCOUNT = "vault-dek";

int64_t now_epoch()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

QString to_qstr(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

std::string from_qstr(QString const& s)
{
    auto utf8 = s.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

} // namespace

LocalVaultProvider::LocalVaultProvider(PasswordsStore& store, QObject* parent)
    : QObject(parent)
    , m_store(store)
{
    m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
}

LocalVaultProvider::~LocalVaultProvider()
{
    clear_decrypted_memory();
}

ProviderCapabilities LocalVaultProvider::capabilities() const
{
    return {
        .can_sync = false,
        .can_create = true,
        .can_update = true,
        .supports_biometric_unlock = has_biometric_unlock(),
        .supports_totp = true,
    };
}

bool LocalVaultProvider::is_configured() const
{
    return m_store.get_meta_string(STORE_NS, "kdf_meta").has_value()
        && m_store.get_meta_string(STORE_NS, "wrapped_dek").has_value();
}

bool LocalVaultProvider::has_biometric_unlock() const
{
    if (!is_configured() || !Keyring::is_available())
        return false;
    auto enabled = m_store.get_meta_string(STORE_NS, "biometric_enabled");
    return enabled.has_value() && *enabled == "1" && Keyring::has_secret(KEYRING_SERVICE, KEYRING_ACCOUNT);
}

bool LocalVaultProvider::setup_vault(std::string_view master_passphrase, bool enable_biometric, std::string& error_out)
{
    if (master_passphrase.size() < 4) {
        error_out = "Master passphrase must be at least 4 characters.";
        return false;
    }

    auto kdf = LocalVaultCrypto::create_default_kdf_params();
    auto dek = LocalVaultCrypto::generate_dek();
    if (!kdf || !dek) {
        error_out = "Failed to generate vault encryption keys.";
        return false;
    }

    auto kek = LocalVaultCrypto::derive_kek(master_passphrase, *kdf);
    if (!kek) {
        error_out = "Failed to derive key from master passphrase.";
        return false;
    }

    auto wrapped = LocalVaultCrypto::wrap_dek(kek->span(), dek->span());
    kek->clear();
    if (!wrapped) {
        error_out = "Failed to wrap vault encryption key.";
        return false;
    }

    QJsonObject kdf_obj;
    kdf_obj["iterations"] = static_cast<int>(kdf->iterations);
    kdf_obj["memory_kib"] = static_cast<int>(kdf->memory_kib);
    kdf_obj["parallelism"] = static_cast<int>(kdf->parallelism);
    kdf_obj["salt"] = to_qstr(base64_encode(kdf->salt));

    QJsonObject wrap_obj;
    wrap_obj["nonce"] = to_qstr(base64_encode(wrapped->nonce));
    wrap_obj["ciphertext"] = to_qstr(base64_encode(wrapped->ciphertext));
    wrap_obj["tag"] = to_qstr(base64_encode(wrapped->tag));

    m_store.clear_local_vault_items();
    bool ok = m_store.set_meta_string(STORE_NS, "kdf_meta", QJsonDocument(kdf_obj).toJson(QJsonDocument::Compact).toStdString())
        && m_store.set_meta_string(STORE_NS, "wrapped_dek", QJsonDocument(wrap_obj).toJson(QJsonDocument::Compact).toStdString());
    if (!ok) {
        error_out = "Failed to save vault metadata to Passwords.db.";
        return false;
    }

    clear_decrypted_memory();
    m_dek = std::move(*dek);
    m_state = ProviderState::Unlocked;
    m_last_error.clear();

    if (enable_biometric && Keyring::is_available()) {
        if (Keyring::store_secret(KEYRING_SERVICE, KEYRING_ACCOUNT, m_dek.span(), true))
            m_store.set_meta_string(STORE_NS, "biometric_enabled", "1");
        else
            m_store.set_meta_string(STORE_NS, "biometric_enabled", "0");
    } else {
        m_store.set_meta_string(STORE_NS, "biometric_enabled", "0");
        Keyring::delete_secret(KEYRING_SERVICE, KEYRING_ACCOUNT);
    }

    if (on_changed)
        on_changed();
    return true;
}

bool LocalVaultProvider::enable_biometric_unlock(bool enable)
{
    if (m_state != ProviderState::Unlocked || m_dek.size() != LocalVaultCrypto::KEY_LENGTH)
        return false;
    if (enable) {
        if (!Keyring::store_secret(KEYRING_SERVICE, KEYRING_ACCOUNT, m_dek.span(), true))
            return false;
        m_store.set_meta_string(STORE_NS, "biometric_enabled", "1");
    } else {
        Keyring::delete_secret(KEYRING_SERVICE, KEYRING_ACCOUNT);
        m_store.set_meta_string(STORE_NS, "biometric_enabled", "0");
    }
    if (on_changed)
        on_changed();
    return true;
}

void LocalVaultProvider::unlock(UnlockRequest const& request, StatusCallback callback)
{
    if (!is_configured()) {
        m_state = ProviderState::Unconfigured;
        if (callback)
            callback(false, "Local Vault is not set up yet.");
        return;
    }

    if (request.method == UnlockMethod::Biometric) {
        m_state = ProviderState::Unlocking;
        if (on_changed)
            on_changed();
        Keyring::load_secret_async(
            KEYRING_SERVICE,
            KEYRING_ACCOUNT,
            "Unlock Superbird Local Vault",
            [this, callback = std::move(callback)](std::optional<SecureBytes> secret, std::string error) {
                if (!secret || secret->size() != LocalVaultCrypto::KEY_LENGTH) {
                    m_state = ProviderState::Locked;
                    m_last_error = error.empty() ? "Biometric unlock failed." : error;
                    if (on_changed)
                        on_changed();
                    if (callback)
                        callback(false, m_last_error);
                    return;
                }
                m_dek = std::move(*secret);
                if (!load_and_decrypt_all_items()) {
                    clear_decrypted_memory();
                    m_state = ProviderState::Locked;
                    m_last_error = "Failed to decrypt local vault items.";
                    if (on_changed)
                        on_changed();
                    if (callback)
                        callback(false, m_last_error);
                    return;
                }
                m_state = ProviderState::Unlocked;
                m_last_error.clear();
                if (on_changed)
                    on_changed();
                if (callback)
                    callback(true, {});
            });
        return;
    }

    auto kdf_str = m_store.get_meta_string(STORE_NS, "kdf_meta");
    auto wrap_str = m_store.get_meta_string(STORE_NS, "wrapped_dek");
    if (!kdf_str || !wrap_str) {
        m_last_error = "Vault metadata is missing.";
        if (callback)
            callback(false, m_last_error);
        return;
    }

    auto kdf_doc = QJsonDocument::fromJson(QByteArray::fromStdString(*kdf_str)).object();
    auto wrap_doc = QJsonDocument::fromJson(QByteArray::fromStdString(*wrap_str)).object();

    auto salt = base64_decode(from_qstr(kdf_doc["salt"].toString()));
    auto nonce = base64_decode(from_qstr(wrap_doc["nonce"].toString()));
    auto ct = base64_decode(from_qstr(wrap_doc["ciphertext"].toString()));
    auto tag = base64_decode(from_qstr(wrap_doc["tag"].toString()));

    if (!salt || salt->size() != 16 || !nonce || nonce->size() != 12 || !ct || !tag || tag->size() != 16) {
        m_last_error = "Corrupt vault key metadata.";
        if (callback)
            callback(false, m_last_error);
        return;
    }

    LocalVaultKdfParams params;
    params.iterations = static_cast<uint32_t>(kdf_doc["iterations"].toInt(3));
    params.memory_kib = static_cast<uint32_t>(kdf_doc["memory_kib"].toInt(65536));
    params.parallelism = static_cast<uint32_t>(kdf_doc["parallelism"].toInt(4));
    std::memcpy(params.salt.data(), salt->data(), 16);

    LocalVaultSealedBlob wrapped;
    std::memcpy(wrapped.nonce.data(), nonce->data(), 12);
    wrapped.ciphertext = std::move(*ct);
    std::memcpy(wrapped.tag.data(), tag->data(), 16);

    auto kek = LocalVaultCrypto::derive_kek(request.master_password, params);
    if (!kek) {
        m_last_error = "Failed to derive decryption key.";
        if (callback)
            callback(false, m_last_error);
        return;
    }

    auto dek = LocalVaultCrypto::unwrap_dek(kek->span(), wrapped);
    kek->clear();
    if (!dek) {
        m_state = ProviderState::Locked;
        m_last_error = "Incorrect master passphrase.";
        if (on_changed)
            on_changed();
        if (callback)
            callback(false, m_last_error);
        return;
    }

    m_dek = std::move(*dek);
    if (!load_and_decrypt_all_items()) {
        clear_decrypted_memory();
        m_state = ProviderState::Locked;
        m_last_error = "Failed to decrypt local vault items.";
        if (on_changed)
            on_changed();
        if (callback)
            callback(false, m_last_error);
        return;
    }

    m_state = ProviderState::Unlocked;
    m_last_error.clear();
    if (on_changed)
        on_changed();
    if (callback)
        callback(true, {});
}

void LocalVaultProvider::lock()
{
    clear_decrypted_memory();
    m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
    m_last_error.clear();
    if (on_changed)
        on_changed();
}

void LocalVaultProvider::sync(StatusCallback callback)
{
    if (callback)
        callback(m_state == ProviderState::Unlocked, {});
}

std::vector<CredentialSummary> LocalVaultProvider::all_credentials() const
{
    std::vector<CredentialSummary> out;
    if (m_state != ProviderState::Unlocked)
        return out;
    out.reserve(m_items.size());
    for (auto const& [_, item] : m_items)
        out.push_back(item.summary);
    return out;
}

std::vector<CredentialSummary> LocalVaultProvider::match_for_url(std::string_view url) const
{
    if (m_state != ProviderState::Unlocked)
        return {};
    return UriMatcher::filter_and_rank(url, all_credentials());
}

std::vector<CredentialSummary> LocalVaultProvider::search(std::string_view query) const
{
    if (m_state != ProviderState::Unlocked)
        return {};
    return UriMatcher::search(query, all_credentials());
}

void LocalVaultProvider::reveal_secret(std::string_view id, SecretCallback callback)
{
    if (!callback)
        return;
    if (m_state != ProviderState::Unlocked) {
        callback(std::nullopt, "Local Vault is locked.");
        return;
    }
    auto it = m_items.find(std::string(id));
    if (it == m_items.end()) {
        callback(std::nullopt, "Credential not found.");
        return;
    }
    callback(it->second.secret, {});
}

void LocalVaultProvider::create_login(NewLogin const& login, WriteCallback callback)
{
    if (m_state != ProviderState::Unlocked || m_dek.size() != LocalVaultCrypto::KEY_LENGTH) {
        if (callback)
            callback(std::nullopt, "Local Vault is locked.");
        return;
    }

    DecryptedItem item;
    auto now = now_epoch();
    item.created_at = now;
    item.updated_at = now;
    item.summary.provider = ProviderKind::LocalVault;
    item.summary.provider_account_id = account_id();
    item.summary.id = generate_item_id();
    item.summary.name = login.name.empty() ? (login.uris.empty() ? login.username : login.uris[0].uri) : login.name;
    item.summary.username = login.username;
    item.summary.uris = login.uris;
    item.summary.has_password = !login.password.empty();
    item.summary.has_totp = login.totp_uri.has_value() && !login.totp_uri->empty();
    item.summary.favorite = login.favorite;

    item.secret.username = login.username;
    item.secret.password = login.password;
    item.secret.totp_uri = login.totp_uri;
    item.secret.notes = login.notes;

    if (!persist_item(item)) {
        if (callback)
            callback(std::nullopt, "Failed to encrypt and save login.");
        return;
    }

    auto summary = item.summary;
    m_items[item.summary.id] = std::move(item);
    if (on_changed)
        on_changed();
    if (callback)
        callback(std::move(summary), {});
}

void LocalVaultProvider::update_login(std::string_view id, UpdatedLogin const& update, WriteCallback callback)
{
    if (m_state != ProviderState::Unlocked || m_dek.size() != LocalVaultCrypto::KEY_LENGTH) {
        if (callback)
            callback(std::nullopt, "Local Vault is locked.");
        return;
    }

    auto it = m_items.find(std::string(id));
    if (it == m_items.end()) {
        if (callback)
            callback(std::nullopt, "Credential not found.");
        return;
    }

    DecryptedItem updated = it->second;
    updated.updated_at = now_epoch();
    if (update.name.has_value())
        updated.summary.name = *update.name;
    if (update.username.has_value()) {
        updated.summary.username = *update.username;
        updated.secret.username = *update.username;
    }
    if (update.password.has_value()) {
        if (!updated.secret.password.empty() && updated.secret.password != *update.password) {
            updated.password_history.push_back(updated.secret.password);
            if (updated.password_history.size() > 10)
                updated.password_history.erase(updated.password_history.begin());
        }
        updated.secret.password = *update.password;
        updated.summary.has_password = !updated.secret.password.empty();
    }
    if (update.uris.has_value())
        updated.summary.uris = *update.uris;
    if (update.totp_uri.has_value()) {
        updated.secret.totp_uri = update.totp_uri->empty() ? std::nullopt : update.totp_uri;
        updated.summary.has_totp = updated.secret.totp_uri.has_value();
    }
    if (update.notes.has_value())
        updated.secret.notes = update.notes->empty() ? std::nullopt : update.notes;
    if (update.favorite.has_value())
        updated.summary.favorite = *update.favorite;

    if (!persist_item(updated)) {
        if (callback)
            callback(std::nullopt, "Failed to update encrypted login.");
        return;
    }

    auto summary = updated.summary;
    it->second = std::move(updated);
    if (on_changed)
        on_changed();
    if (callback)
        callback(std::move(summary), {});
}

bool LocalVaultProvider::delete_login(std::string_view id)
{
    if (m_state != ProviderState::Unlocked)
        return false;
    auto it = m_items.find(std::string(id));
    if (it == m_items.end())
        return false;
    if (!m_store.delete_local_vault_item(id))
        return false;
    cleanse_string(it->second.secret.password);
    if (it->second.secret.totp_uri)
        cleanse_string(*it->second.secret.totp_uri);
    m_items.erase(it);
    if (on_changed)
        on_changed();
    return true;
}

bool LocalVaultProvider::reset_vault()
{
    clear_decrypted_memory();
    Keyring::delete_secret(KEYRING_SERVICE, KEYRING_ACCOUNT);
    m_store.clear_local_vault_items();
    m_store.delete_namespace(STORE_NS);
    m_state = ProviderState::Unconfigured;
    m_last_error.clear();
    if (on_changed)
        on_changed();
    return true;
}

CsvImportResult LocalVaultProvider::import_csv(std::string_view csv_text, size_t& imported_count)
{
    imported_count = 0;
    auto parsed = CsvTransfer::import_csv(csv_text);
    if (m_state != ProviderState::Unlocked)
        return parsed;

    for (auto const& login : parsed.logins) {
        bool saved = false;
        create_login(login, [&](std::optional<CredentialSummary> s, std::string const&) {
            saved = s.has_value();
        });
        if (saved)
            ++imported_count;
    }
    return parsed;
}

std::optional<std::string> LocalVaultProvider::export_csv(CsvFormat format) const
{
    if (m_state != ProviderState::Unlocked)
        return std::nullopt;
    std::vector<NewLogin> logins;
    logins.reserve(m_items.size());
    for (auto const& [_, item] : m_items) {
        NewLogin l;
        l.name = item.summary.name;
        l.username = item.secret.username;
        l.password = item.secret.password;
        l.uris = item.summary.uris;
        l.totp_uri = item.secret.totp_uri;
        l.notes = item.secret.notes;
        l.favorite = item.summary.favorite;
        logins.push_back(std::move(l));
    }
    return CsvTransfer::export_csv(logins, format);
}

bool LocalVaultProvider::load_and_decrypt_all_items()
{
    m_items.clear();
    auto rows = m_store.load_local_vault_items();
    for (auto const& row : rows) {
        auto json = LocalVaultCrypto::decrypt_item(m_dek.span(), row.id, row.sealed);
        if (!json)
            return false;
        auto item = parse_item_json(row.id, row.created_at, row.updated_at, *json);
        cleanse_string(*json);
        if (item)
            m_items.emplace(item->summary.id, std::move(*item));
    }
    return true;
}

bool LocalVaultProvider::persist_item(DecryptedItem const& item)
{
    auto json = serialize_item_json(item);
    auto sealed = LocalVaultCrypto::encrypt_item(m_dek.span(), item.summary.id, json);
    cleanse_string(json);
    if (!sealed)
        return false;

    StoredLocalVaultRow row;
    row.id = item.summary.id;
    row.created_at = item.created_at;
    row.updated_at = item.updated_at;
    row.sealed = std::move(*sealed);
    return m_store.upsert_local_vault_item(row);
}

void LocalVaultProvider::clear_decrypted_memory()
{
    m_dek.clear();
    for (auto& [_, item] : m_items) {
        cleanse_string(item.secret.password);
        cleanse_string(item.secret.username);
        if (item.secret.totp_uri)
            cleanse_string(*item.secret.totp_uri);
        if (item.secret.notes)
            cleanse_string(*item.secret.notes);
        for (auto& h : item.password_history)
            cleanse_string(h);
    }
    m_items.clear();
}

std::string LocalVaultProvider::serialize_item_json(DecryptedItem const& item)
{
    QJsonObject obj;
    obj["name"] = to_qstr(item.summary.name);
    obj["username"] = to_qstr(item.secret.username);
    obj["password"] = to_qstr(item.secret.password);
    obj["favorite"] = item.summary.favorite;
    if (item.secret.totp_uri.has_value())
        obj["totp_uri"] = to_qstr(*item.secret.totp_uri);
    if (item.secret.notes.has_value())
        obj["notes"] = to_qstr(*item.secret.notes);

    QJsonArray uris;
    for (auto const& u : item.summary.uris) {
        QJsonObject uo;
        uo["uri"] = to_qstr(u.uri);
        uo["match"] = static_cast<int>(u.match_type);
        uris.append(uo);
    }
    obj["uris"] = uris;

    QJsonArray hist;
    for (auto const& pw : item.password_history)
        hist.append(to_qstr(pw));
    obj["password_history"] = hist;

    return QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
}

std::optional<LocalVaultProvider::DecryptedItem> LocalVaultProvider::parse_item_json(
    std::string_view id,
    int64_t created_at,
    int64_t updated_at,
    std::string_view json)
{
    auto doc = QJsonDocument::fromJson(QByteArray(json.data(), static_cast<qsizetype>(json.size())));
    if (!doc.isObject())
        return std::nullopt;
    auto obj = doc.object();

    DecryptedItem item;
    item.created_at = created_at;
    item.updated_at = updated_at;
    item.summary.provider = ProviderKind::LocalVault;
    item.summary.provider_account_id = "local";
    item.summary.id = std::string(id);
    item.summary.name = from_qstr(obj["name"].toString());
    item.summary.username = from_qstr(obj["username"].toString());
    item.summary.favorite = obj["favorite"].toBool(false);

    item.secret.username = item.summary.username;
    item.secret.password = from_qstr(obj["password"].toString());
    item.summary.has_password = !item.secret.password.empty();

    if (obj.contains("totp_uri")) {
        auto t = from_qstr(obj["totp_uri"].toString());
        if (!t.empty()) {
            item.secret.totp_uri = std::move(t);
            item.summary.has_totp = true;
        }
    }
    if (obj.contains("notes")) {
        auto n = from_qstr(obj["notes"].toString());
        if (!n.empty())
            item.secret.notes = std::move(n);
    }

    for (auto const& val : obj["uris"].toArray()) {
        auto uo = val.toObject();
        auto uri = from_qstr(uo["uri"].toString());
        if (uri.empty())
            continue;
        int m = uo["match"].toInt(0);
        auto mt = (m >= 0 && m <= 5) ? static_cast<UriMatchType>(m) : UriMatchType::BaseDomain;
        item.summary.uris.push_back({ std::move(uri), mt });
    }
    for (auto const& val : obj["password_history"].toArray()) {
        auto pw = from_qstr(val.toString());
        if (!pw.empty())
            item.password_history.push_back(std::move(pw));
    }
    return item;
}

std::string LocalVaultProvider::generate_item_id()
{
    uint8_t bytes[16] {};
    random_bytes(bytes);
    static constexpr char HEX[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out.push_back('-');
        out.push_back(HEX[(bytes[i] >> 4) & 0x0F]);
        out.push_back(HEX[bytes[i] & 0x0F]);
    }
    return out;
}

}
