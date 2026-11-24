/*
 * Copyright (c) 2026, Sidelady contributors.
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
#include <AK/Span.h>
#include <AK/kmalloc.h>
#include <LibDatabase/Database.h>
#include <UI/Qt/SessionStore.h>
#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabModel.h>

namespace Ladybird {

// Sidecar SQLite store for tree-tab parent links and collapsed state, keyed by SessionTabId.
// Kept intentionally separate from upstream SessionStore so SessionStore schemas never conflict on rebase.
class TreeTabStore {
    AK_MAKE_NONCOPYABLE(TreeTabStore);
    AK_MAKE_NONMOVABLE(TreeTabStore);

public:
    AK_ALLOC_WITH_KMALLOC;

    static ErrorOr<NonnullOwnPtr<TreeTabStore>> create(ByteString const& data_directory);
    static NonnullOwnPtr<TreeTabStore> create_transient();
    ~TreeTabStore() = default;

    bool is_persistent() const { return m_database != nullptr; }

    HashMap<SessionTabId, PersistedTreeNodeState> load_all() const;
    void sync_window_nodes(SessionWindowId window_id, ReadonlySpan<PersistedTreeNodeState> nodes);
    void remove_tab(SessionTabId session_tab_id);
    void remove_window(SessionWindowId window_id);

private:
    struct Statements {
        Database::StatementID select_all { 0 };
        Database::StatementID upsert_node { 0 };
        Database::StatementID delete_tab { 0 };
        Database::StatementID delete_window { 0 };
    };

    TreeTabStore() = default;
    TreeTabStore(NonnullRefPtr<Database::Database>, Statements);

    RefPtr<Database::Database> m_database;
    Statements m_statements;
};

}
