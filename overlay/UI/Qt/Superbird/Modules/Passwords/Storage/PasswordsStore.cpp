/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/ByteBuffer.h>
#include <AK/Format.h>
#include <AK/OwnPtr.h>
#include <LibDatabase/ResultRow.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/Passwords/Storage/PasswordsStore.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace Superbird::Passwords {

namespace {

constexpr auto PASSWORDS_STORE_NAME = "Passwords"sv;
constexpr size_t MAX_TEXT_BYTES = 1024 * 1024;
constexpr size_t MAX_BLOB_BYTES = 8 * 1024 * 1024;

ErrorOr<String> to_ak_string(std::string_view sv)
{
    return String::from_utf8(StringView { sv.data(), sv.size() });
}

std::string to_std_string(String const& s)
{
    auto v = s.bytes_as_string_view();
    return std::string(v.characters_without_null_termination(), v.length());
}

int64_t now_epoch_seconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

PasswordsStore::PasswordsStore(NonnullRefPtr<Database::Database> database, Statements statements)
    : m_database(move(database))
    , m_statements(statements)
{
}

NonnullOwnPtr<PasswordsStore> PasswordsStore::create_transient()
{
    return adopt_own(*new PasswordsStore());
}

ErrorOr<NonnullOwnPtr<PasswordsStore>> PasswordsStore::create(ByteString const& data_directory)
{
    auto database = TRY(Database::Database::create(data_directory, PASSWORDS_STORE_NAME));
    TRY(database->set_busy_timeout(2000));

    auto migrations = to_array<Database::Migration>({
        {
            .version = 1,
            .sql = R"#(
                CREATE TABLE IF NOT EXISTS kv_meta (
                    namespace TEXT NOT NULL,
                    key TEXT NOT NULL,
                    value BLOB NOT NULL,
                    PRIMARY KEY (namespace, key)
                );
                CREATE TABLE IF NOT EXISTS local_vault_items (
                    id TEXT PRIMARY KEY,
                    created_at INTEGER NOT NULL,
                    updated_at INTEGER NOT NULL,
                    nonce BLOB NOT NULL,
                    ciphertext BLOB NOT NULL,
                    tag BLOB NOT NULL
                );
                CREATE TABLE IF NOT EXISTS bw_ciphers (
                    account_id TEXT NOT NULL,
                    cipher_id TEXT NOT NULL,
                    organization_id TEXT NOT NULL DEFAULT '',
                    raw_json TEXT NOT NULL,
                    PRIMARY KEY (account_id, cipher_id)
                );
                CREATE INDEX IF NOT EXISTS idx_bw_ciphers_account
                    ON bw_ciphers(account_id);
                CREATE TABLE IF NOT EXISTS never_save_hosts (
                    host TEXT PRIMARY KEY,
                    created_at INTEGER NOT NULL
                );
            )#",
            .backfill = {},
        },
    });

    if (TRY(database->migrate(PASSWORDS_STORE_NAME, migrations)) != Database::MigrationOutcome::Success)
        return Error::from_string_literal("Passwords database was created by a newer Superbird version");

    Statements st {};
    st.get_meta = TRY(database->prepare_statement("SELECT value FROM kv_meta WHERE namespace = :ns AND key = :key;"sv));
    st.set_meta = TRY(database->prepare_statement(
        "INSERT INTO kv_meta (namespace, key, value) VALUES (:ns, :key, :value) "
        "ON CONFLICT(namespace, key) DO UPDATE SET value = excluded.value;"sv));
    st.delete_meta = TRY(database->prepare_statement("DELETE FROM kv_meta WHERE namespace = :ns AND key = :key;"sv));
    st.delete_namespace = TRY(database->prepare_statement("DELETE FROM kv_meta WHERE namespace = :ns;"sv));

    st.select_local_items = TRY(database->prepare_statement(
        "SELECT id, created_at, updated_at, nonce, ciphertext, tag FROM local_vault_items ORDER BY updated_at DESC;"sv));
    st.upsert_local_item = TRY(database->prepare_statement(
        "INSERT INTO local_vault_items (id, created_at, updated_at, nonce, ciphertext, tag) "
        "VALUES (:id, :created_at, :updated_at, :nonce, :ciphertext, :tag) "
        "ON CONFLICT(id) DO UPDATE SET updated_at = excluded.updated_at, nonce = excluded.nonce, ciphertext = excluded.ciphertext, tag = excluded.tag;"sv));
    st.delete_local_item = TRY(database->prepare_statement("DELETE FROM local_vault_items WHERE id = :id;"sv));
    st.clear_local_items = TRY(database->prepare_statement("DELETE FROM local_vault_items;"sv));

    st.select_bw_ciphers = TRY(database->prepare_statement(
        "SELECT cipher_id, organization_id, raw_json FROM bw_ciphers WHERE account_id = :account_id;"sv));
    st.delete_bw_ciphers = TRY(database->prepare_statement("DELETE FROM bw_ciphers WHERE account_id = :account_id;"sv));
    st.upsert_bw_cipher = TRY(database->prepare_statement(
        "INSERT INTO bw_ciphers (account_id, cipher_id, organization_id, raw_json) "
        "VALUES (:account_id, :cipher_id, :organization_id, :raw_json) "
        "ON CONFLICT(account_id, cipher_id) DO UPDATE SET organization_id = excluded.organization_id, raw_json = excluded.raw_json;"sv));

    st.check_never_host = TRY(database->prepare_statement("SELECT 1 AS found FROM never_save_hosts WHERE host = :host;"sv));
    st.insert_never_host = TRY(database->prepare_statement(
        "INSERT INTO never_save_hosts (host, created_at) VALUES (:host, :created_at) "
        "ON CONFLICT(host) DO NOTHING;"sv));
    st.delete_never_host = TRY(database->prepare_statement("DELETE FROM never_save_hosts WHERE host = :host;"sv));
    st.list_never_hosts = TRY(database->prepare_statement("SELECT host FROM never_save_hosts ORDER BY host ASC;"sv));

    return adopt_own(*new PasswordsStore(move(database), st));
}

PasswordsStore& PasswordsStore::the()
{
    static OwnPtr<PasswordsStore> instance;
    if (!instance) {
        auto const& data_dir = WindowHost::persistent_data_directory();
        if (!data_dir.has_value()) {
            instance = create_transient();
        } else {
            auto created = create(*data_dir);
            if (created.is_error()) {
                dbgln("PasswordsStore: falling back to transient store: {}", created.error());
                instance = create_transient();
            } else {
                instance = created.release_value();
            }
        }
    }
    return *instance;
}

std::optional<std::vector<uint8_t>> PasswordsStore::get_meta(std::string_view ns, std::string_view key)
{
    if (!m_database) {
        auto it = m_mem_meta.find({ std::string(ns), std::string(key) });
        if (it == m_mem_meta.end())
            return std::nullopt;
        return it->second;
    }

    std::optional<std::vector<uint8_t>> out;
    auto res = m_database->try_execute_bound_statement(
        m_statements.get_meta,
        [&](auto& bind) -> ErrorOr<void> {
            TRY(bind("ns"sv, TRY(to_ak_string(ns))));
            TRY(bind("key"sv, TRY(to_ak_string(key))));
            return {};
        },
        [&](Database::ResultRow& row) -> ErrorOr<void> {
            auto buf = TRY(row.read_blob("value"sv, MAX_BLOB_BYTES));
            out.emplace(buf.data(), buf.data() + buf.size());
            return {};
        });
    if (res.is_error())
        return std::nullopt;
    return out;
}

bool PasswordsStore::set_meta(std::string_view ns, std::string_view key, std::span<uint8_t const> value)
{
    if (!m_database) {
        m_mem_meta[{ std::string(ns), std::string(key) }] = std::vector<uint8_t>(value.begin(), value.end());
        return true;
    }

    auto blob = ByteBuffer::copy(value.data(), value.size());
    if (blob.is_error())
        return false;

    auto res = m_database->try_execute_bound_statement(m_statements.set_meta, [&](auto& bind) -> ErrorOr<void> {
        TRY(bind("ns"sv, TRY(to_ak_string(ns))));
        TRY(bind("key"sv, TRY(to_ak_string(key))));
        TRY(bind("value"sv, blob.value().bytes()));
        return {};
    });
    return !res.is_error();
}

bool PasswordsStore::set_meta_string(std::string_view ns, std::string_view key, std::string_view value)
{
    return set_meta(ns, key, { reinterpret_cast<uint8_t const*>(value.data()), value.size() });
}

std::optional<std::string> PasswordsStore::get_meta_string(std::string_view ns, std::string_view key)
{
    auto bytes = get_meta(ns, key);
    if (!bytes)
        return std::nullopt;
    return std::string(reinterpret_cast<char const*>(bytes->data()), bytes->size());
}

bool PasswordsStore::delete_meta(std::string_view ns, std::string_view key)
{
    if (!m_database) {
        m_mem_meta.erase({ std::string(ns), std::string(key) });
        return true;
    }
    auto res = m_database->try_execute_bound_statement(m_statements.delete_meta, [&](auto& bind) -> ErrorOr<void> {
        TRY(bind("ns"sv, TRY(to_ak_string(ns))));
        TRY(bind("key"sv, TRY(to_ak_string(key))));
        return {};
    });
    return !res.is_error();
}

bool PasswordsStore::delete_namespace(std::string_view ns)
{
    if (!m_database) {
        for (auto it = m_mem_meta.begin(); it != m_mem_meta.end();) {
            if (it->first.first == ns)
                it = m_mem_meta.erase(it);
            else
                ++it;
        }
        return true;
    }
    auto res = m_database->try_execute_bound_statement(m_statements.delete_namespace, [&](auto& bind) -> ErrorOr<void> {
        return bind("ns"sv, TRY(to_ak_string(ns)));
    });
    return !res.is_error();
}

std::vector<StoredLocalVaultRow> PasswordsStore::load_local_vault_items()
{
    std::vector<StoredLocalVaultRow> rows;
    if (!m_database) {
        for (auto const& [_, r] : m_mem_local_items)
            rows.push_back(r);
        return rows;
    }

    auto res = m_database->try_execute_bound_statement(
        m_statements.select_local_items,
        [](auto&) -> ErrorOr<void> { return {}; },
        [&](Database::ResultRow& row) -> ErrorOr<void> {
            auto nonce = TRY(row.read_blob("nonce"sv, 64));
            auto ct = TRY(row.read_blob("ciphertext"sv, MAX_BLOB_BYTES));
            auto tag = TRY(row.read_blob("tag"sv, 64));
            if (nonce.size() != 12 || tag.size() != 16)
                return {};
            StoredLocalVaultRow r;
            r.id = to_std_string(TRY(row.read_text("id"sv, 256)));
            r.created_at = TRY(row.read_integer<i64>("created_at"sv));
            r.updated_at = TRY(row.read_integer<i64>("updated_at"sv));
            std::memcpy(r.sealed.nonce.data(), nonce.data(), 12);
            r.sealed.ciphertext.assign(ct.data(), ct.data() + ct.size());
            std::memcpy(r.sealed.tag.data(), tag.data(), 16);
            rows.push_back(std::move(r));
            return {};
        });
    if (res.is_error())
        dbgln("PasswordsStore: load_local_vault_items failed: {}", res.error());
    return rows;
}

bool PasswordsStore::upsert_local_vault_item(StoredLocalVaultRow const& row)
{
    if (!m_database) {
        m_mem_local_items[row.id] = row;
        return true;
    }
    auto res = m_database->try_execute_bound_statement(m_statements.upsert_local_item, [&](auto& bind) -> ErrorOr<void> {
        TRY(bind("id"sv, TRY(to_ak_string(row.id))));
        TRY(bind("created_at"sv, i64 { row.created_at }));
        TRY(bind("updated_at"sv, i64 { row.updated_at }));
        TRY(bind("nonce"sv, ReadonlyBytes { row.sealed.nonce.data(), row.sealed.nonce.size() }));
        TRY(bind("ciphertext"sv, ReadonlyBytes { row.sealed.ciphertext.data(), row.sealed.ciphertext.size() }));
        TRY(bind("tag"sv, ReadonlyBytes { row.sealed.tag.data(), row.sealed.tag.size() }));
        return {};
    });
    return !res.is_error();
}

bool PasswordsStore::delete_local_vault_item(std::string_view id)
{
    if (!m_database) {
        m_mem_local_items.erase(std::string(id));
        return true;
    }
    auto res = m_database->try_execute_bound_statement(m_statements.delete_local_item, [&](auto& bind) -> ErrorOr<void> {
        return bind("id"sv, TRY(to_ak_string(id)));
    });
    return !res.is_error();
}

bool PasswordsStore::clear_local_vault_items()
{
    if (!m_database) {
        m_mem_local_items.clear();
        return true;
    }
    auto res = m_database->try_execute_bound_statement(m_statements.clear_local_items, [](auto&) -> ErrorOr<void> { return {}; });
    return !res.is_error();
}

std::vector<StoredBitwardenCipherRow> PasswordsStore::load_bitwarden_ciphers(std::string_view account_id)
{
    std::vector<StoredBitwardenCipherRow> rows;
    if (!m_database) {
        auto it = m_mem_bw_ciphers.find(std::string(account_id));
        if (it != m_mem_bw_ciphers.end())
            rows = it->second;
        return rows;
    }

    auto res = m_database->try_execute_bound_statement(
        m_statements.select_bw_ciphers,
        [&](auto& bind) -> ErrorOr<void> { return bind("account_id"sv, TRY(to_ak_string(account_id))); },
        [&](Database::ResultRow& row) -> ErrorOr<void> {
            StoredBitwardenCipherRow r;
            r.account_id = std::string(account_id);
            r.cipher_id = to_std_string(TRY(row.read_text("cipher_id"sv, 256)));
            r.organization_id = to_std_string(TRY(row.read_text("organization_id"sv, 256)));
            r.raw_json = to_std_string(TRY(row.read_text("raw_json"sv, MAX_TEXT_BYTES)));
            rows.push_back(std::move(r));
            return {};
        });
    if (res.is_error())
        dbgln("PasswordsStore: load_bitwarden_ciphers failed: {}", res.error());
    return rows;
}

bool PasswordsStore::replace_bitwarden_ciphers(std::string_view account_id, std::vector<StoredBitwardenCipherRow> const& rows)
{
    if (!m_database) {
        m_mem_bw_ciphers[std::string(account_id)] = rows;
        return true;
    }

    auto res = m_database->transaction([&]() -> ErrorOr<void> {
        TRY(m_database->try_execute_bound_statement(m_statements.delete_bw_ciphers, [&](auto& bind) -> ErrorOr<void> {
            return bind("account_id"sv, TRY(to_ak_string(account_id)));
        }));
        for (auto const& r : rows) {
            TRY(m_database->try_execute_bound_statement(m_statements.upsert_bw_cipher, [&](auto& bind) -> ErrorOr<void> {
                TRY(bind("account_id"sv, TRY(to_ak_string(account_id))));
                TRY(bind("cipher_id"sv, TRY(to_ak_string(r.cipher_id))));
                TRY(bind("organization_id"sv, TRY(to_ak_string(r.organization_id))));
                TRY(bind("raw_json"sv, TRY(to_ak_string(r.raw_json))));
                return {};
            }));
        }
        return {};
    });
    return !res.is_error();
}

bool PasswordsStore::upsert_bitwarden_cipher(StoredBitwardenCipherRow const& row)
{
    if (!m_database) {
        auto& vec = m_mem_bw_ciphers[row.account_id];
        for (auto& existing : vec) {
            if (existing.cipher_id == row.cipher_id) {
                existing = row;
                return true;
            }
        }
        vec.push_back(row);
        return true;
    }
    auto res = m_database->try_execute_bound_statement(m_statements.upsert_bw_cipher, [&](auto& bind) -> ErrorOr<void> {
        TRY(bind("account_id"sv, TRY(to_ak_string(row.account_id))));
        TRY(bind("cipher_id"sv, TRY(to_ak_string(row.cipher_id))));
        TRY(bind("organization_id"sv, TRY(to_ak_string(row.organization_id))));
        TRY(bind("raw_json"sv, TRY(to_ak_string(row.raw_json))));
        return {};
    });
    return !res.is_error();
}

bool PasswordsStore::clear_bitwarden_ciphers(std::string_view account_id)
{
    if (!m_database) {
        m_mem_bw_ciphers.erase(std::string(account_id));
        return true;
    }
    auto res = m_database->try_execute_bound_statement(m_statements.delete_bw_ciphers, [&](auto& bind) -> ErrorOr<void> {
        return bind("account_id"sv, TRY(to_ak_string(account_id)));
    });
    return !res.is_error();
}

bool PasswordsStore::is_never_save_host(std::string_view host)
{
    std::string lower(host);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!m_database)
        return m_mem_never_hosts.contains(lower);

    bool found = false;
    auto res = m_database->try_execute_bound_statement(
        m_statements.check_never_host,
        [&](auto& bind) -> ErrorOr<void> { return bind("host"sv, TRY(to_ak_string(lower))); },
        [&](Database::ResultRow&) -> ErrorOr<void> {
            found = true;
            return {};
        });
    if (res.is_error())
        dbgln("PasswordsStore: is_never_save_host failed: {}", res.error());
    return found;
}

bool PasswordsStore::add_never_save_host(std::string_view host)
{
    std::string lower(host);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.empty())
        return false;
    if (!m_database) {
        m_mem_never_hosts.insert(lower);
        return true;
    }
    auto res = m_database->try_execute_bound_statement(m_statements.insert_never_host, [&](auto& bind) -> ErrorOr<void> {
        TRY(bind("host"sv, TRY(to_ak_string(lower))));
        TRY(bind("created_at"sv, i64 { now_epoch_seconds() }));
        return {};
    });
    return !res.is_error();
}

bool PasswordsStore::remove_never_save_host(std::string_view host)
{
    std::string lower(host);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!m_database) {
        m_mem_never_hosts.erase(lower);
        return true;
    }
    auto res = m_database->try_execute_bound_statement(m_statements.delete_never_host, [&](auto& bind) -> ErrorOr<void> {
        return bind("host"sv, TRY(to_ak_string(lower)));
    });
    return !res.is_error();
}

std::vector<std::string> PasswordsStore::list_never_save_hosts()
{
    std::vector<std::string> hosts;
    if (!m_database) {
        hosts.assign(m_mem_never_hosts.begin(), m_mem_never_hosts.end());
        return hosts;
    }
    auto res = m_database->try_execute_bound_statement(
        m_statements.list_never_hosts,
        [](auto&) -> ErrorOr<void> { return {}; },
        [&](Database::ResultRow& row) -> ErrorOr<void> {
            hosts.push_back(to_std_string(TRY(row.read_text("host"sv, 512))));
            return {};
        });
    if (res.is_error())
        dbgln("PasswordsStore: list_never_save_hosts failed: {}", res.error());
    return hosts;
}

}
