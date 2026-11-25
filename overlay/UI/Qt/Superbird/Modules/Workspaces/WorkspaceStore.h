/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/RefPtr.h>
#include <AK/kmalloc.h>
#include <LibDatabase/Database.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceModel.h>

namespace Superbird {

// Sidecar SQLite store (Workspaces.db) for each window's workspaces and which workspace each tab belongs to, keyed by
// SessionWindowId and SessionTabId. Kept apart from Sessions.db, whose schema Superbird never changes.
class WorkspaceStore {
    AK_MAKE_NONCOPYABLE(WorkspaceStore);
    AK_MAKE_NONMOVABLE(WorkspaceStore);

public:
    AK_ALLOC_WITH_KMALLOC;

    static ErrorOr<NonnullOwnPtr<WorkspaceStore>> create(ByteString const& data_directory);
    static NonnullOwnPtr<WorkspaceStore> create_transient();

    // The app's persistent store (or a transient one if it can't be opened), and a transient one for private windows.
    static WorkspaceStore& the(bool is_private);

    bool is_persistent() const { return m_database != nullptr; }

    Workspaces::WorkspaceRows load_window(Workspaces::WindowKey);
    // Replaces everything stored for the window with these rows.
    void save_window(Workspaces::WindowKey, Workspaces::WorkspaceRows const&);

private:
    struct Statements {
        Database::StatementID select_workspaces { 0 };
        Database::StatementID select_assignments { 0 };
        Database::StatementID delete_workspaces { 0 };
        Database::StatementID delete_assignments { 0 };
        Database::StatementID insert_workspace { 0 };
        Database::StatementID upsert_assignment { 0 };
    };

    WorkspaceStore() = default;
    WorkspaceStore(NonnullRefPtr<Database::Database>, Statements);

    RefPtr<Database::Database> m_database;
    Statements m_statements;
    // A transient store keeps its rows in memory instead.
    Workspaces::WorkspaceRows m_memory;
};

}
