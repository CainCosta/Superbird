/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/RefPtr.h>
#include <AK/kmalloc.h>
#include <LibDatabase/Database.h>

namespace Superbird {

// Sidecar SQLite store (SplitView.db) for each window's split group, as the text SplitLayout::serialize() writes, keyed
// by SessionWindowId. Kept apart from Sessions.db, whose schema Superbird never changes.
class SplitStore {
    AK_MAKE_NONCOPYABLE(SplitStore);
    AK_MAKE_NONMOVABLE(SplitStore);

public:
    AK_ALLOC_WITH_KMALLOC;

    // The app's persistent store (or a transient one if it can't be opened), and a transient one for private windows.
    static SplitStore& the(bool is_private);

    Optional<ByteString> load_window(i64 window);
    // Stores the window's split, or forgets it given nothing.
    void save_window(i64 window, Optional<ByteString> const& layout);

private:
    struct Statements {
        Database::StatementID select_layout { 0 };
        Database::StatementID upsert_layout { 0 };
        Database::StatementID delete_layout { 0 };
    };

    static ErrorOr<NonnullOwnPtr<SplitStore>> create(ByteString const& data_directory);
    static NonnullOwnPtr<SplitStore> create_transient();

    SplitStore() = default;
    SplitStore(NonnullRefPtr<Database::Database>, Statements);

    RefPtr<Database::Database> m_database;
    Statements m_statements;
    HashMap<i64, ByteString> m_memory;
};

}
