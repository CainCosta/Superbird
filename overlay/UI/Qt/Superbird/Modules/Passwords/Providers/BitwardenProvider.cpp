/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/UriMatcher.h>
#include <UI/Qt/Superbird/Modules/Passwords/Keyring/Keyring.h>
#include <UI/Qt/Superbird/Modules/Passwords/Providers/BitwardenProvider.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>

#include <curl/curl.h>

#include <chrono>
#include <mutex>
#include <thread>

namespace Superbird::Passwords {

namespace {

constexpr std::string_view STORE_NS = "bitwarden";
constexpr std::string_view KEYRING_SERVICE = "org.superbird.Superbird.Bitwarden";
constexpr int PERIODIC_SYNC_INTERVAL_MS = 15 * 60 * 1000;

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

BitwardenProvider::BitwardenProvider(PasswordsStore& store, QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_periodic_sync_timer(new QTimer(this))
{
    static std::once_flag curl_init_once;
    std::call_once(curl_init_once, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
    m_periodic_sync_timer->setInterval(PERIODIC_SYNC_INTERVAL_MS);
    connect(m_periodic_sync_timer, &QTimer::timeout, this, [this] {
        if (m_state == ProviderState::Unlocked)
            sync({});
    });

    load_persisted_account_config();
    m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
}

BitwardenProvider::~BitwardenProvider()
{
    clear_decrypted_memory();
}

std::string BitwardenProvider::display_label() const
{
    if (m_email.empty())
        return "Bitwarden";
    return "Bitwarden (" + m_email + ")";
}

ProviderCapabilities BitwardenProvider::capabilities() const
{
    return {
        .can_sync = true,
        .can_create = true,
        .can_update = true,
        .supports_biometric_unlock = has_biometric_unlock(),
        .supports_totp = true,
    };
}

bool BitwardenProvider::is_configured() const
{
    return !m_email.empty() && !m_protected_symmetric_key.empty();
}

bool BitwardenProvider::has_biometric_unlock() const
{
    return is_configured() && m_biometric_enabled && Keyring::is_available()
        && Keyring::has_secret(KEYRING_SERVICE, m_email);
}

std::string BitwardenProvider::ensure_device_identifier()
{
    if (!m_device_identifier.empty())
        return m_device_identifier;
    if (auto saved = m_store.get_meta_string(STORE_NS, "device_id"); saved && !saved->empty()) {
        m_device_identifier = *saved;
        return m_device_identifier;
    }
    uint8_t b[16] {};
    random_bytes(b);
    static constexpr char HEX[] = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            id.push_back('-');
        id.push_back(HEX[(b[i] >> 4) & 0x0F]);
        id.push_back(HEX[b[i] & 0x0F]);
    }
    m_device_identifier = id;
    m_store.set_meta_string(STORE_NS, "device_id", id);
    return m_device_identifier;
}

void BitwardenProvider::load_persisted_account_config()
{
    auto cfg_str = m_store.get_meta_string(STORE_NS, "account_config");
    if (!cfg_str)
        return;
    auto obj = QJsonDocument::fromJson(QByteArray::fromStdString(*cfg_str)).object();
    m_region = static_cast<BitwardenRegion>(obj["region"].toInt(0));
    m_custom_server_url = from_qstr(obj["custom_server_url"].toString());
    m_email = from_qstr(obj["email"].toString());
    m_device_identifier = from_qstr(obj["device_identifier"].toString());
    m_protected_symmetric_key = from_qstr(obj["protected_symmetric_key"].toString());
    m_encrypted_private_key = from_qstr(obj["encrypted_private_key"].toString());
    m_biometric_enabled = obj["biometric_enabled"].toBool(false);
    m_last_sync_time = obj["last_sync_time"].toInteger(0);

    m_kdf.type = static_cast<BitwardenKdfType>(obj["kdf_type"].toInt(0));
    m_kdf.iterations = static_cast<uint32_t>(obj["kdf_iterations"].toInt(600000));
    m_kdf.memory_mb = static_cast<uint32_t>(obj["kdf_memory_mb"].toInt(64));
    m_kdf.parallelism = static_cast<uint32_t>(obj["kdf_parallelism"].toInt(4));

    m_encrypted_org_keys.clear();
    auto orgs = obj["encrypted_org_keys"].toObject();
    for (auto it = orgs.begin(); it != orgs.end(); ++it)
        m_encrypted_org_keys.emplace(from_qstr(it.key()), from_qstr(it.value().toString()));
}

void BitwardenProvider::save_persisted_account_config()
{
    QJsonObject obj;
    obj["region"] = static_cast<int>(m_region);
    obj["custom_server_url"] = to_qstr(m_custom_server_url);
    obj["email"] = to_qstr(m_email);
    obj["device_identifier"] = to_qstr(ensure_device_identifier());
    obj["protected_symmetric_key"] = to_qstr(m_protected_symmetric_key);
    obj["encrypted_private_key"] = to_qstr(m_encrypted_private_key);
    obj["biometric_enabled"] = m_biometric_enabled;
    obj["last_sync_time"] = static_cast<qint64>(m_last_sync_time);
    obj["kdf_type"] = static_cast<int>(m_kdf.type);
    obj["kdf_iterations"] = static_cast<int>(m_kdf.iterations);
    obj["kdf_memory_mb"] = static_cast<int>(m_kdf.memory_mb);
    obj["kdf_parallelism"] = static_cast<int>(m_kdf.parallelism);

    QJsonObject orgs;
    for (auto const& [oid, okey] : m_encrypted_org_keys)
        orgs[to_qstr(oid)] = to_qstr(okey);
    obj["encrypted_org_keys"] = orgs;

    m_store.set_meta_string(STORE_NS, "account_config", QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString());
}

void BitwardenProvider::save_tokens_encrypted()
{
    if (!m_user_key.is_valid())
        return;
    QJsonObject obj;
    obj["refresh_token"] = to_qstr(m_refresh_token);
    obj["api_client_id"] = to_qstr(m_api_client_id);
    obj["api_client_secret"] = to_qstr(m_api_client_secret);
    std::string plain = QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
    auto enc = BitwardenCrypto::encrypt_string(plain, m_user_key);
    cleanse_string(plain);
    if (enc)
        m_store.set_meta_string(STORE_NS, "encrypted_tokens", *enc);
}

void BitwardenProvider::load_tokens_encrypted()
{
    if (!m_user_key.is_valid())
        return;
    auto enc = m_store.get_meta_string(STORE_NS, "encrypted_tokens");
    if (!enc)
        return;
    auto plain = BitwardenCrypto::decrypt_string(*enc, m_user_key);
    if (!plain)
        return;
    auto obj = QJsonDocument::fromJson(QByteArray::fromStdString(*plain)).object();
    cleanse_string(*plain);
    m_refresh_token = from_qstr(obj["refresh_token"].toString());
    m_api_client_id = from_qstr(obj["api_client_id"].toString());
    m_api_client_secret = from_qstr(obj["api_client_secret"].toString());
}

namespace {

size_t curl_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    // Cap response size at 32 MiB to prevent unbounded memory use.
    if (out->size() + total > 32u * 1024u * 1024u)
        return 0;
    out->append(ptr, total);
    return total;
}

} // namespace

void BitwardenProvider::send_http(
    BitwardenHttpRequest const& req,
    std::function<void(int status, std::string body, std::string net_error)> callback)
{
    QPointer<BitwardenProvider> guard(this);
    BitwardenHttpRequest req_copy = req;

    std::thread([guard, req_copy = std::move(req_copy), callback = std::move(callback)]() mutable {
        int http_status = 0;
        std::string response_body;
        std::string net_err;

        CURL* curl = curl_easy_init();
        if (!curl) {
            net_err = "Failed to initialize HTTP client.";
        } else {
            curl_easy_setopt(curl, CURLOPT_URL, req_copy.url.c_str());
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef CURLSSLOPT_NATIVE_CA
            curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

            struct curl_slist* headers = nullptr;
            for (auto const& [k, v] : req_copy.headers) {
                std::string line = k + ": " + v;
                headers = curl_slist_append(headers, line.c_str());
            }
            if (headers)
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            if (req_copy.method == "POST") {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req_copy.body.data());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(req_copy.body.size()));
            } else if (req_copy.method == "PUT") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req_copy.body.data());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(req_copy.body.size()));
            }

            char errbuf[CURL_ERROR_SIZE] {};
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

            CURLcode rc = curl_easy_perform(curl);
            if (rc == CURLE_OK) {
                long code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
                http_status = static_cast<int>(code);
            } else {
                net_err = errbuf[0] ? std::string(errbuf) : std::string(curl_easy_strerror(rc));
            }

            if (headers)
                curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }

        cleanse_string(req_copy.body);
        for (auto& [_, v] : req_copy.headers)
            cleanse_string(v);

        if (!guard)
            return;

        QMetaObject::invokeMethod(
            guard.data(),
            [guard, http_status, body = std::move(response_body), net_err = std::move(net_err), callback = std::move(callback)]() mutable {
                if (!guard || !callback)
                    return;
                callback(http_status, std::move(body), std::move(net_err));
            },
            Qt::QueuedConnection);
    }).detach();
}

void BitwardenProvider::login_with_password(
    BitwardenRegion region,
    std::string_view custom_server_url,
    std::string_view email,
    std::string_view master_password,
    std::optional<int> two_factor_provider,
    std::optional<std::string_view> two_factor_token,
    std::optional<std::string_view> new_device_otp,
    bool enable_biometric,
    BitwardenLoginCallback callback)
{
    auto norm_email = BitwardenCrypto::normalize_email(email);
    if (norm_email.empty() || master_password.empty()) {
        if (callback)
            callback({ false, std::nullopt, "Email and master password are required." });
        return;
    }

    auto endpoints = BitwardenEndpoints::for_region(region, custom_server_url);
    m_state = ProviderState::Unlocking;
    if (on_changed)
        on_changed();

    auto pre_req = BitwardenProtocol::build_prelogin_request(endpoints, norm_email);
    std::string pw_copy(master_password);
    std::string custom_copy(custom_server_url);
    std::optional<std::string> tf_tok = two_factor_token ? std::optional<std::string>(*two_factor_token) : std::nullopt;
    std::optional<std::string> nd_otp = new_device_otp ? std::optional<std::string>(*new_device_otp) : std::nullopt;

    send_http(pre_req, [this, region, custom_copy, norm_email, endpoints, pw_copy, two_factor_provider, tf_tok, nd_otp, enable_biometric, callback = std::move(callback)](int status, std::string body, std::string net_err) mutable {
        if (!net_err.empty() || status < 200 || status >= 300) {
            cleanse_string(pw_copy);
            m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
            m_last_error = !net_err.empty() ? net_err : ("Prelogin failed (HTTP " + std::to_string(status) + ")");
            if (on_changed)
                on_changed();
            if (callback)
                callback({ false, std::nullopt, m_last_error });
            return;
        }

        auto kdf = BitwardenProtocol::parse_prelogin_response(body);
        if (!kdf) {
            cleanse_string(pw_copy);
            m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
            m_last_error = "Invalid prelogin response from server.";
            if (on_changed)
                on_changed();
            if (callback)
                callback({ false, std::nullopt, m_last_error });
            return;
        }

        auto mk = BitwardenCrypto::derive_master_key(pw_copy, norm_email, *kdf);
        if (!mk) {
            cleanse_string(pw_copy);
            m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
            m_last_error = "Failed to derive Bitwarden master key.";
            if (on_changed)
                on_changed();
            if (callback)
                callback({ false, std::nullopt, m_last_error });
            return;
        }

        auto mp_hash = BitwardenCrypto::derive_master_password_hash(mk->span(), pw_copy);
        auto stretched = BitwardenCrypto::stretch_master_key(mk->span());
        cleanse_string(pw_copy);
        mk->clear();

        if (!mp_hash || !stretched) {
            m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
            m_last_error = "Failed to compute Bitwarden login hash.";
            if (on_changed)
                on_changed();
            if (callback)
                callback({ false, std::nullopt, m_last_error });
            return;
        }

        auto tok_req = BitwardenProtocol::build_password_token_request(
            endpoints,
            norm_email,
            *mp_hash,
            ensure_device_identifier(),
            "Superbird",
            two_factor_provider,
            tf_tok ? std::optional<std::string_view>(*tf_tok) : std::nullopt,
            nd_otp ? std::optional<std::string_view>(*nd_otp) : std::nullopt);

        auto stretched_ptr = std::make_shared<BitwardenSymmetricKey>(std::move(*stretched));
        send_http(tok_req, [this, region, custom_copy, norm_email, kdf = *kdf, stretched_ptr, enable_biometric, callback = std::move(callback)](int tstatus, std::string tbody, std::string tnet_err) mutable {
            if (!tnet_err.empty()) {
                stretched_ptr->clear();
                m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
                m_last_error = tnet_err;
                if (on_changed)
                    on_changed();
                if (callback)
                    callback({ false, std::nullopt, m_last_error });
                return;
            }

            auto tok = BitwardenProtocol::parse_token_response(tstatus, tbody);
            if (!tok.ok) {
                stretched_ptr->clear();
                m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
                m_last_error = tok.error;
                if (on_changed)
                    on_changed();
                if (callback)
                    callback({ false, tok.challenge, tok.error });
                return;
            }

            auto user_key_bytes = BitwardenCrypto::decrypt_bytes(tok.protected_symmetric_key, *stretched_ptr);
            stretched_ptr->clear();
            auto user_key = user_key_bytes ? BitwardenSymmetricKey::from_bytes(user_key_bytes->span()) : std::nullopt;
            if (!user_key) {
                m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
                m_last_error = "Failed to unwrap Bitwarden vault key.";
                if (on_changed)
                    on_changed();
                if (callback)
                    callback({ false, std::nullopt, m_last_error });
                return;
            }

            m_region = region;
            m_custom_server_url = custom_copy;
            m_email = norm_email;
            m_kdf = kdf;
            m_protected_symmetric_key = tok.protected_symmetric_key;
            if (!tok.private_key.empty())
                m_encrypted_private_key = tok.private_key;
            m_user_key = std::move(*user_key);
            m_access_token = tok.access_token;
            m_refresh_token = tok.refresh_token;
            m_access_token_expiry = now_epoch() + tok.expires_in - 60;

            if (enable_biometric && Keyring::is_available()) {
                auto key_bytes = m_user_key.to_bytes();
                m_biometric_enabled = Keyring::store_secret(KEYRING_SERVICE, m_email, key_bytes.span(), true);
            } else {
                m_biometric_enabled = false;
                Keyring::delete_secret(KEYRING_SERVICE, m_email);
            }

            save_persisted_account_config();
            save_tokens_encrypted();

            m_state = ProviderState::Syncing;
            m_last_error.clear();
            if (on_changed)
                on_changed();

            sync([this, callback = std::move(callback)](bool sync_ok, std::string sync_err) {
                if (sync_ok)
                    m_periodic_sync_timer->start();
                if (callback)
                    callback({ sync_ok, std::nullopt, sync_err });
            });
        });
    });
}

void BitwardenProvider::login_with_api_key(
    BitwardenRegion region,
    std::string_view custom_server_url,
    std::string_view email,
    std::string_view client_id,
    std::string_view client_secret,
    std::string_view master_password,
    bool enable_biometric,
    BitwardenLoginCallback callback)
{
    auto norm_email = BitwardenCrypto::normalize_email(email);
    if (norm_email.empty() || client_id.empty() || client_secret.empty() || master_password.empty()) {
        if (callback)
            callback({ false, std::nullopt, "Email, Client ID, Client Secret, and Master Password are required." });
        return;
    }

    auto endpoints = BitwardenEndpoints::for_region(region, custom_server_url);
    m_state = ProviderState::Unlocking;
    if (on_changed)
        on_changed();

    auto req = BitwardenProtocol::build_api_key_token_request(endpoints, client_id, client_secret, ensure_device_identifier(), "Superbird");
    std::string custom_copy(custom_server_url);
    std::string cid_copy(client_id);
    std::string csec_copy(client_secret);
    std::string pw_copy(master_password);

    send_http(req, [this, region, custom_copy, norm_email, cid_copy, csec_copy, pw_copy, enable_biometric, callback = std::move(callback)](int status, std::string body, std::string net_err) mutable {
        if (!net_err.empty()) {
            cleanse_string(pw_copy);
            cleanse_string(csec_copy);
            m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
            m_last_error = net_err;
            if (on_changed)
                on_changed();
            if (callback)
                callback({ false, std::nullopt, m_last_error });
            return;
        }

        auto tok = BitwardenProtocol::parse_token_response(status, body);
        if (!tok.ok) {
            cleanse_string(pw_copy);
            cleanse_string(csec_copy);
            m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
            m_last_error = tok.error;
            if (on_changed)
                on_changed();
            if (callback)
                callback({ false, std::nullopt, tok.error });
            return;
        }

        auto mk = BitwardenCrypto::derive_master_key(pw_copy, norm_email, tok.kdf);
        cleanse_string(pw_copy);
        auto stretched = mk ? BitwardenCrypto::stretch_master_key(mk->span()) : std::nullopt;
        if (mk)
            mk->clear();
        auto user_key_bytes = (stretched && !tok.protected_symmetric_key.empty())
            ? BitwardenCrypto::decrypt_bytes(tok.protected_symmetric_key, *stretched)
            : std::nullopt;
        if (stretched)
            stretched->clear();
        auto user_key = user_key_bytes ? BitwardenSymmetricKey::from_bytes(user_key_bytes->span()) : std::nullopt;

        if (!user_key) {
            cleanse_string(csec_copy);
            m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
            m_last_error = "API key succeeded, but master password failed to unlock vault key.";
            if (on_changed)
                on_changed();
            if (callback)
                callback({ false, std::nullopt, m_last_error });
            return;
        }

        m_region = region;
        m_custom_server_url = custom_copy;
        m_email = norm_email;
        m_kdf = tok.kdf;
        m_protected_symmetric_key = tok.protected_symmetric_key;
        if (!tok.private_key.empty())
            m_encrypted_private_key = tok.private_key;
        m_user_key = std::move(*user_key);
        m_access_token = tok.access_token;
        m_access_token_expiry = now_epoch() + tok.expires_in - 60;
        m_api_client_id = cid_copy;
        m_api_client_secret = csec_copy;
        cleanse_string(csec_copy);

        if (enable_biometric && Keyring::is_available()) {
            auto key_bytes = m_user_key.to_bytes();
            m_biometric_enabled = Keyring::store_secret(KEYRING_SERVICE, m_email, key_bytes.span(), true);
        } else {
            m_biometric_enabled = false;
            Keyring::delete_secret(KEYRING_SERVICE, m_email);
        }

        save_persisted_account_config();
        save_tokens_encrypted();

        m_state = ProviderState::Syncing;
        m_last_error.clear();
        if (on_changed)
            on_changed();

        sync([this, callback = std::move(callback)](bool sync_ok, std::string sync_err) {
            if (sync_ok)
                m_periodic_sync_timer->start();
            if (callback)
                callback({ sync_ok, std::nullopt, sync_err });
        });
    });
}

void BitwardenProvider::unlock(UnlockRequest const& request, StatusCallback callback)
{
    if (!is_configured()) {
        m_state = ProviderState::Unconfigured;
        if (callback)
            callback(false, "Bitwarden account is not configured.");
        return;
    }

    auto finish_unlock = [this, callback = std::move(callback)](BitwardenSymmetricKey key) {
        m_user_key = std::move(key);
        load_tokens_encrypted();
        m_org_keys = BitwardenProtocol::unwrap_organization_keys(m_encrypted_private_key, m_encrypted_org_keys, m_user_key);
        decrypt_cached_ciphers();
        m_state = ProviderState::Unlocked;
        m_last_error.clear();
        m_periodic_sync_timer->start();
        if (on_changed)
            on_changed();
        if (callback)
            callback(true, {});
        // Trigger background sync to pick up remote changes while offline cache is already unlocked.
        sync({});
    };

    if (request.method == UnlockMethod::Biometric) {
        m_state = ProviderState::Unlocking;
        if (on_changed)
            on_changed();
        Keyring::load_secret_async(
            KEYRING_SERVICE,
            m_email,
            "Unlock Bitwarden (" + m_email + ")",
            [this, finish_unlock = std::move(finish_unlock), callback](std::optional<SecureBytes> secret, std::string error) mutable {
                auto key = secret ? BitwardenSymmetricKey::from_bytes(secret->span()) : std::nullopt;
                if (!key) {
                    m_state = ProviderState::Locked;
                    m_last_error = error.empty() ? "Biometric unlock failed." : error;
                    if (on_changed)
                        on_changed();
                    if (callback)
                        callback(false, m_last_error);
                    return;
                }
                finish_unlock(std::move(*key));
            });
        return;
    }

    auto mk = BitwardenCrypto::derive_master_key(request.master_password, m_email, m_kdf);
    auto stretched = mk ? BitwardenCrypto::stretch_master_key(mk->span()) : std::nullopt;
    if (mk)
        mk->clear();
    auto raw_key = stretched ? BitwardenCrypto::decrypt_bytes(m_protected_symmetric_key, *stretched) : std::nullopt;
    if (stretched)
        stretched->clear();
    auto user_key = raw_key ? BitwardenSymmetricKey::from_bytes(raw_key->span()) : std::nullopt;
    if (!user_key) {
        m_state = ProviderState::Locked;
        m_last_error = "Incorrect Bitwarden master password.";
        if (on_changed)
            on_changed();
        if (callback)
            callback(false, m_last_error);
        return;
    }

    finish_unlock(std::move(*user_key));
}

void BitwardenProvider::lock()
{
    m_periodic_sync_timer->stop();
    clear_decrypted_memory();
    m_state = is_configured() ? ProviderState::Locked : ProviderState::Unconfigured;
    m_last_error.clear();
    if (on_changed)
        on_changed();
}

void BitwardenProvider::sign_out()
{
    m_periodic_sync_timer->stop();
    if (!m_email.empty()) {
        Keyring::delete_secret(KEYRING_SERVICE, m_email);
        m_store.clear_bitwarden_ciphers(m_email);
    }
    m_store.delete_namespace(STORE_NS);
    clear_decrypted_memory();
    m_email.clear();
    m_custom_server_url.clear();
    m_protected_symmetric_key.clear();
    m_encrypted_private_key.clear();
    m_encrypted_org_keys.clear();
    m_biometric_enabled = false;
    m_last_sync_time = 0;
    m_state = ProviderState::Unconfigured;
    m_last_error.clear();
    if (on_changed)
        on_changed();
}

void BitwardenProvider::ensure_valid_access_token(std::function<void(bool ok, std::string error)> callback)
{
    if (!m_access_token.empty() && now_epoch() < m_access_token_expiry) {
        if (callback)
            callback(true, {});
        return;
    }

    auto endpoints = BitwardenEndpoints::for_region(m_region, m_custom_server_url);
    std::optional<BitwardenHttpRequest> req;
    if (!m_refresh_token.empty()) {
        req = BitwardenProtocol::build_refresh_token_request(endpoints, m_refresh_token);
    } else if (!m_api_client_id.empty() && !m_api_client_secret.empty()) {
        req = BitwardenProtocol::build_api_key_token_request(endpoints, m_api_client_id, m_api_client_secret, ensure_device_identifier(), "Superbird");
    }

    if (!req) {
        if (callback)
            callback(false, "Session expired; sign in again to sync.");
        return;
    }

    send_http(*req, [this, callback = std::move(callback)](int status, std::string body, std::string net_err) {
        if (!net_err.empty()) {
            if (callback)
                callback(false, net_err);
            return;
        }
        auto tok = BitwardenProtocol::parse_token_response(status, body);
        if (!tok.ok) {
            if (callback)
                callback(false, tok.error);
            return;
        }
        m_access_token = tok.access_token;
        if (!tok.refresh_token.empty())
            m_refresh_token = tok.refresh_token;
        m_access_token_expiry = now_epoch() + tok.expires_in - 60;
        save_tokens_encrypted();
        if (callback)
            callback(true, {});
    });
}

void BitwardenProvider::sync(StatusCallback callback)
{
    if (!m_user_key.is_valid()) {
        if (callback)
            callback(false, "Bitwarden vault is locked.");
        return;
    }

    ensure_valid_access_token([this, callback = std::move(callback)](bool ok, std::string err) {
        if (!ok) {
            m_state = ProviderState::Unlocked;
            m_last_error = err;
            if (on_changed)
                on_changed();
            if (callback)
                callback(false, err);
            return;
        }

        auto endpoints = BitwardenEndpoints::for_region(m_region, m_custom_server_url);
        auto req = BitwardenProtocol::build_sync_request(endpoints, m_access_token);
        send_http(req, [this, callback = std::move(callback)](int status, std::string body, std::string net_err) {
            if (!net_err.empty()) {
                m_state = ProviderState::Unlocked;
                m_last_error = net_err;
                if (on_changed)
                    on_changed();
                if (callback)
                    callback(false, net_err);
                return;
            }
            auto parsed = BitwardenProtocol::parse_sync_response(status, body);
            if (!parsed.ok) {
                m_state = ProviderState::Unlocked;
                m_last_error = parsed.error;
                if (on_changed)
                    on_changed();
                if (callback)
                    callback(false, parsed.error);
                return;
            }

            apply_sync_result(parsed);
            m_state = ProviderState::Unlocked;
            m_last_error.clear();
            if (on_changed)
                on_changed();
            if (callback)
                callback(true, {});
        });
    });
}

void BitwardenProvider::apply_sync_result(BitwardenSyncResult const& sync_result)
{
    if (!sync_result.protected_symmetric_key.empty())
        m_protected_symmetric_key = sync_result.protected_symmetric_key;
    if (!sync_result.encrypted_private_key.empty())
        m_encrypted_private_key = sync_result.encrypted_private_key;
    if (!sync_result.encrypted_org_keys.empty())
        m_encrypted_org_keys = sync_result.encrypted_org_keys;
    m_last_sync_time = now_epoch();
    save_persisted_account_config();

    m_org_keys = BitwardenProtocol::unwrap_organization_keys(m_encrypted_private_key, m_encrypted_org_keys, m_user_key);

    std::vector<StoredBitwardenCipherRow> rows;
    rows.reserve(sync_result.ciphers.size());
    for (auto const& c : sync_result.ciphers) {
        rows.push_back({
            .account_id = m_email,
            .cipher_id = c.id,
            .organization_id = c.organization_id,
            .raw_json = serialize_raw_cipher(c),
        });
    }
    m_store.replace_bitwarden_ciphers(m_email, rows);
    decrypt_cached_ciphers();
}

void BitwardenProvider::decrypt_cached_ciphers()
{
    for (auto& [_, entry] : m_logins) {
        cleanse_string(entry.secret.password);
        if (entry.secret.totp_uri)
            cleanse_string(*entry.secret.totp_uri);
    }
    m_logins.clear();

    if (!m_user_key.is_valid())
        return;

    auto rows = m_store.load_bitwarden_ciphers(m_email);
    for (auto const& row : rows) {
        auto raw = deserialize_raw_cipher(row.raw_json);
        if (!raw)
            continue;
        auto dec = BitwardenProtocol::decrypt_cipher(*raw, m_user_key, m_org_keys, m_email);
        if (dec)
            m_logins.emplace(dec->summary.id, std::move(*dec));
    }
}

std::vector<CredentialSummary> BitwardenProvider::all_credentials() const
{
    std::vector<CredentialSummary> out;
    if (m_state != ProviderState::Unlocked)
        return out;
    out.reserve(m_logins.size());
    for (auto const& [_, l] : m_logins)
        out.push_back(l.summary);
    return out;
}

std::vector<CredentialSummary> BitwardenProvider::match_for_url(std::string_view url) const
{
    if (m_state != ProviderState::Unlocked)
        return {};
    return UriMatcher::filter_and_rank(url, all_credentials());
}

std::vector<CredentialSummary> BitwardenProvider::search(std::string_view query) const
{
    if (m_state != ProviderState::Unlocked)
        return {};
    return UriMatcher::search(query, all_credentials());
}

void BitwardenProvider::reveal_secret(std::string_view id, SecretCallback callback)
{
    if (!callback)
        return;
    if (m_state != ProviderState::Unlocked) {
        callback(std::nullopt, "Bitwarden vault is locked.");
        return;
    }
    auto it = m_logins.find(std::string(id));
    if (it == m_logins.end()) {
        callback(std::nullopt, "Bitwarden item not found.");
        return;
    }
    callback(it->second.secret, {});
}

void BitwardenProvider::create_login(NewLogin const& login, WriteCallback callback)
{
    if (m_state != ProviderState::Unlocked || !m_user_key.is_valid()) {
        if (callback)
            callback(std::nullopt, "Bitwarden vault is locked.");
        return;
    }

    auto login_copy = login;
    ensure_valid_access_token([this, login_copy = std::move(login_copy), callback = std::move(callback)](bool ok, std::string err) {
        if (!ok) {
            if (callback)
                callback(std::nullopt, err);
            return;
        }
        auto endpoints = BitwardenEndpoints::for_region(m_region, m_custom_server_url);
        auto req = BitwardenProtocol::build_create_cipher_request(endpoints, m_access_token, login_copy, m_user_key);
        if (!req) {
            if (callback)
                callback(std::nullopt, "Failed to encrypt Bitwarden login.");
            return;
        }
        send_http(*req, [this, callback = std::move(callback)](int status, std::string body, std::string net_err) {
            if (!net_err.empty()) {
                if (callback)
                    callback(std::nullopt, net_err);
                return;
            }
            auto raw = BitwardenProtocol::parse_single_cipher_response(status, body);
            if (!raw) {
                if (callback)
                    callback(std::nullopt, "Bitwarden rejected creating login (HTTP " + std::to_string(status) + ").");
                return;
            }
            m_store.upsert_bitwarden_cipher({
                .account_id = m_email,
                .cipher_id = raw->id,
                .organization_id = raw->organization_id,
                .raw_json = serialize_raw_cipher(*raw),
            });
            auto dec = BitwardenProtocol::decrypt_cipher(*raw, m_user_key, m_org_keys, m_email);
            if (!dec) {
                if (callback)
                    callback(std::nullopt, "Saved remotely, but failed to decrypt response.");
                return;
            }
            auto summary = dec->summary;
            m_logins[summary.id] = std::move(*dec);
            if (on_changed)
                on_changed();
            if (callback)
                callback(std::move(summary), {});
        });
    });
}

void BitwardenProvider::update_login(std::string_view id, UpdatedLogin const& update, WriteCallback callback)
{
    if (m_state != ProviderState::Unlocked || !m_user_key.is_valid()) {
        if (callback)
            callback(std::nullopt, "Bitwarden vault is locked.");
        return;
    }

    auto it = m_logins.find(std::string(id));
    if (it == m_logins.end()) {
        if (callback)
            callback(std::nullopt, "Bitwarden login not found.");
        return;
    }

    NewLogin merged;
    merged.name = update.name.value_or(it->second.summary.name);
    merged.username = update.username.value_or(it->second.secret.username);
    merged.password = update.password.value_or(it->second.secret.password);
    merged.uris = update.uris.value_or(it->second.summary.uris);
    merged.totp_uri = update.totp_uri.has_value() ? update.totp_uri : it->second.secret.totp_uri;
    merged.notes = update.notes.has_value() ? update.notes : it->second.secret.notes;
    merged.favorite = update.favorite.value_or(it->second.summary.favorite);

    std::string id_str(id);
    ensure_valid_access_token([this, id_str = std::move(id_str), merged = std::move(merged), callback = std::move(callback)](bool ok, std::string err) {
        if (!ok) {
            if (callback)
                callback(std::nullopt, err);
            return;
        }
        auto endpoints = BitwardenEndpoints::for_region(m_region, m_custom_server_url);
        auto req = BitwardenProtocol::build_update_cipher_request(endpoints, m_access_token, id_str, merged, m_user_key);
        if (!req) {
            if (callback)
                callback(std::nullopt, "Failed to encrypt updated Bitwarden login.");
            return;
        }
        send_http(*req, [this, callback = std::move(callback)](int status, std::string body, std::string net_err) {
            if (!net_err.empty()) {
                if (callback)
                    callback(std::nullopt, net_err);
                return;
            }
            auto raw = BitwardenProtocol::parse_single_cipher_response(status, body);
            if (!raw) {
                if (callback)
                    callback(std::nullopt, "Bitwarden rejected updating login (HTTP " + std::to_string(status) + ").");
                return;
            }
            m_store.upsert_bitwarden_cipher({
                .account_id = m_email,
                .cipher_id = raw->id,
                .organization_id = raw->organization_id,
                .raw_json = serialize_raw_cipher(*raw),
            });
            auto dec = BitwardenProtocol::decrypt_cipher(*raw, m_user_key, m_org_keys, m_email);
            if (!dec) {
                if (callback)
                    callback(std::nullopt, "Updated remotely, but failed to decrypt response.");
                return;
            }
            auto summary = dec->summary;
            m_logins[summary.id] = std::move(*dec);
            if (on_changed)
                on_changed();
            if (callback)
                callback(std::move(summary), {});
        });
    });
}

void BitwardenProvider::clear_decrypted_memory()
{
    m_user_key.clear();
    for (auto& [_, k] : m_org_keys)
        k.clear();
    m_org_keys.clear();
    cleanse_string(m_access_token);
    cleanse_string(m_refresh_token);
    cleanse_string(m_api_client_id);
    cleanse_string(m_api_client_secret);
    m_access_token_expiry = 0;

    for (auto& [_, entry] : m_logins) {
        cleanse_string(entry.secret.password);
        cleanse_string(entry.secret.username);
        if (entry.secret.totp_uri)
            cleanse_string(*entry.secret.totp_uri);
        if (entry.secret.notes)
            cleanse_string(*entry.secret.notes);
    }
    m_logins.clear();
}

std::string BitwardenProvider::serialize_raw_cipher(BitwardenRawCipher const& c)
{
    QJsonObject obj;
    obj["id"] = to_qstr(c.id);
    obj["organizationId"] = to_qstr(c.organization_id);
    obj["type"] = c.type;
    obj["key"] = to_qstr(c.encrypted_key);
    obj["name"] = to_qstr(c.encrypted_name);
    obj["notes"] = to_qstr(c.encrypted_notes);
    obj["favorite"] = c.favorite;

    QJsonObject login;
    login["username"] = to_qstr(c.encrypted_username);
    login["password"] = to_qstr(c.encrypted_password);
    login["totp"] = to_qstr(c.encrypted_totp);
    QJsonArray uris;
    for (auto const& u : c.uris) {
        QJsonObject uo;
        uo["uri"] = to_qstr(u.encrypted_uri);
        uo["match"] = static_cast<int>(u.match_type);
        uris.append(uo);
    }
    login["uris"] = uris;
    obj["login"] = login;
    return QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
}

std::optional<BitwardenRawCipher> BitwardenProvider::deserialize_raw_cipher(std::string_view json)
{
    auto doc = QJsonDocument::fromJson(QByteArray(json.data(), static_cast<qsizetype>(json.size())));
    if (!doc.isObject())
        return std::nullopt;
    auto obj = doc.object();
    BitwardenRawCipher c;
    c.id = from_qstr(obj["id"].toString());
    if (c.id.empty())
        return std::nullopt;
    c.organization_id = from_qstr(obj["organizationId"].toString());
    c.type = obj["type"].toInt(1);
    c.encrypted_key = from_qstr(obj["key"].toString());
    c.encrypted_name = from_qstr(obj["name"].toString());
    c.encrypted_notes = from_qstr(obj["notes"].toString());
    c.favorite = obj["favorite"].toBool(false);

    auto login = obj["login"].toObject();
    c.encrypted_username = from_qstr(login["username"].toString());
    c.encrypted_password = from_qstr(login["password"].toString());
    c.encrypted_totp = from_qstr(login["totp"].toString());
    for (auto const& val : login["uris"].toArray()) {
        auto uo = val.toObject();
        auto uri = from_qstr(uo["uri"].toString());
        if (uri.empty())
            continue;
        int m = uo["match"].toInt(0);
        c.uris.push_back({ std::move(uri), (m >= 0 && m <= 5) ? static_cast<UriMatchType>(m) : UriMatchType::BaseDomain });
    }
    return c;
}

}
