/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/Noncopyable.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/RefPtr.h>
#include <AK/kmalloc.h>
#include <LibDatabase/Database.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/LocalVaultCrypto.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Superbird::Passwords {

struct StoredLocalVaultRow {
    std::string id;
    int64_t created_at { 0 };
    int64_t updated_at { 0 };
    LocalVaultSealedBlob sealed;
};

struct StoredBitwardenCipherRow {
    std::string account_id;
    std::string cipher_id;
    std::string organization_id;
    std::string raw_json;
};

// Sidecar SQLite store (`Passwords.db`) inside WindowHost::persistent_data_directory().
// Never modifies upstream Sessions.db or Bookmarks.db; never writes plaintext credentials on disk.
class PasswordsStore {
    AK_MAKE_NONCOPYABLE(PasswordsStore);
    AK_MAKE_NONMOVABLE(PasswordsStore);

public:
    AK_ALLOC_WITH_KMALLOC;

    static PasswordsStore& the();
    static ErrorOr<NonnullOwnPtr<PasswordsStore>> create(ByteString const& data_directory);
    static NonnullOwnPtr<PasswordsStore> create_transient();

    // Generic key-value metadata (namespaces: "local_vault", "bitwarden", "settings").
    std::optional<std::vector<uint8_t>> get_meta(std::string_view ns, std::string_view key);
    bool set_meta(std::string_view ns, std::string_view key, std::span<uint8_t const> value);
    bool set_meta_string(std::string_view ns, std::string_view key, std::string_view value);
    std::optional<std::string> get_meta_string(std::string_view ns, std::string_view key);
    bool delete_meta(std::string_view ns, std::string_view key);
    bool delete_namespace(std::string_view ns);

    // Local Vault encrypted rows.
    std::vector<StoredLocalVaultRow> load_local_vault_items();
    bool upsert_local_vault_item(StoredLocalVaultRow const& row);
    bool delete_local_vault_item(std::string_view id);
    bool clear_local_vault_items();

    // Bitwarden raw server ciphertext cache.
    std::vector<StoredBitwardenCipherRow> load_bitwarden_ciphers(std::string_view account_id);
    bool replace_bitwarden_ciphers(std::string_view account_id, std::vector<StoredBitwardenCipherRow> const& rows);
    bool upsert_bitwarden_cipher(StoredBitwardenCipherRow const& row);
    bool clear_bitwarden_ciphers(std::string_view account_id);

    // "Never save for this site" list.
    bool is_never_save_host(std::string_view host);
    bool add_never_save_host(std::string_view host);
    bool remove_never_save_host(std::string_view host);
    std::vector<std::string> list_never_save_hosts();

private:
    struct Statements {
        Database::StatementID get_meta { 0 };
        Database::StatementID set_meta { 0 };
        Database::StatementID delete_meta { 0 };
        Database::StatementID delete_namespace { 0 };
        Database::StatementID select_local_items { 0 };
        Database::StatementID upsert_local_item { 0 };
        Database::StatementID delete_local_item { 0 };
        Database::StatementID clear_local_items { 0 };
        Database::StatementID select_bw_ciphers { 0 };
        Database::StatementID delete_bw_ciphers { 0 };
        Database::StatementID upsert_bw_cipher { 0 };
        Database::StatementID check_never_host { 0 };
        Database::StatementID insert_never_host { 0 };
        Database::StatementID delete_never_host { 0 };
        Database::StatementID list_never_hosts { 0 };
    };

    PasswordsStore() = default;
    PasswordsStore(NonnullRefPtr<Database::Database>, Statements);

    RefPtr<Database::Database> m_database;
    Statements m_statements {};

    // In-memory fallback when running transiently (no persistent profile dir).
    std::map<std::pair<std::string, std::string>, std::vector<uint8_t>> m_mem_meta;
    std::map<std::string, StoredLocalVaultRow> m_mem_local_items;
    std::map<std::string, std::vector<StoredBitwardenCipherRow>> m_mem_bw_ciphers;
    std::set<std::string> m_mem_never_hosts;
};

}
