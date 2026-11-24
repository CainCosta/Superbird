/*
 * Copyright (c) 2026, Sidelady contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/Format.h>
#include <UI/Qt/TreeTabs/TreeTabStore.h>

namespace Ladybird {

static constexpr auto TREE_TAB_STORE_NAME = "TreeTabs"sv;

TreeTabStore::TreeTabStore(NonnullRefPtr<Database::Database> database, Statements statements)
    : m_database(move(database))
    , m_statements(statements)
{
}

NonnullOwnPtr<TreeTabStore> TreeTabStore::create_transient()
{
    return adopt_own(*new TreeTabStore());
}

ErrorOr<NonnullOwnPtr<TreeTabStore>> TreeTabStore::create(ByteString const& data_directory)
{
    auto database = TRY(Database::Database::create(data_directory, TREE_TAB_STORE_NAME));
    TRY(database->set_busy_timeout(2000));

    auto migrations = to_array<Database::Migration>({
        {
            .version = 1,
            .sql = R"#(
                CREATE TABLE IF NOT EXISTS TreeTabNodes (
                    session_tab_id INTEGER PRIMARY KEY,
                    session_window_id INTEGER NOT NULL,
                    parent_session_tab_id INTEGER,
                    collapsed INTEGER NOT NULL DEFAULT 0
                );
                CREATE INDEX IF NOT EXISTS idx_tree_tab_nodes_window
                    ON TreeTabNodes(session_window_id);
            )#",
            .backfill = {},
        },
    });

    if (TRY(database->migrate(TREE_TAB_STORE_NAME, migrations)) != Database::MigrationOutcome::Success)
        return Error::from_string_literal("TreeTabs database was created by a newer Sidelady version");

    Statements statements {};
    statements.select_all = TRY(database->prepare_statement("SELECT session_tab_id, parent_session_tab_id, collapsed FROM TreeTabNodes;"sv));
    statements.upsert_node = TRY(database->prepare_statement("INSERT INTO TreeTabNodes (session_tab_id, session_window_id, parent_session_tab_id, collapsed) VALUES (?, ?, ?, ?) ON CONFLICT(session_tab_id) DO UPDATE SET session_window_id = excluded.session_window_id, parent_session_tab_id = excluded.parent_session_tab_id, collapsed = excluded.collapsed;"sv));
    statements.delete_tab = TRY(database->prepare_statement("DELETE FROM TreeTabNodes WHERE session_tab_id = ?;"sv));
    statements.delete_window = TRY(database->prepare_statement("DELETE FROM TreeTabNodes WHERE session_window_id = ?;"sv));

    return adopt_own(*new TreeTabStore(move(database), statements));
}

HashMap<SessionTabId, PersistedTreeNodeState> TreeTabStore::load_all() const
{
    HashMap<SessionTabId, PersistedTreeNodeState> result;
    if (!m_database)
        return result;

    auto query_result = m_database->try_execute_statement(m_statements.select_all, [&](auto statement_id) -> ErrorOr<void> {
        auto session_tab_id = m_database->result_column<i64>(statement_id, 0);
        auto parent_session_tab_id = m_database->result_column<Optional<i64>>(statement_id, 1);
        auto collapsed = m_database->result_column<i64>(statement_id, 2) != 0;

        result.set(session_tab_id, PersistedTreeNodeState {
            .session_tab_id = session_tab_id,
            .parent_session_tab_id = parent_session_tab_id,
            .collapsed = collapsed,
        });
        return {};
    });
    if (query_result.is_error())
        dbgln("TreeTabStore: failed to load persisted tree state: {}", query_result.error());

    return result;
}

void TreeTabStore::sync_window_nodes(SessionWindowId window_id, ReadonlySpan<PersistedTreeNodeState> nodes)
{
    if (!m_database)
        return;

    auto result = m_database->transaction([&]() -> ErrorOr<void> {
        TRY(m_database->try_execute_statement(m_statements.delete_window, {}, window_id));
        for (auto const& node : nodes) {
            TRY(m_database->try_execute_statement(
                m_statements.upsert_node,
                {},
                node.session_tab_id,
                window_id,
                node.parent_session_tab_id,
                static_cast<i64>(node.collapsed ? 1 : 0)));
        }
        return {};
    });
    if (result.is_error())
        dbgln("TreeTabStore: failed to sync window {} tree state: {}", window_id, result.error());
}

void TreeTabStore::remove_tab(SessionTabId session_tab_id)
{
    if (!m_database)
        return;

    if (auto result = m_database->try_execute_statement(m_statements.delete_tab, {}, session_tab_id); result.is_error())
        dbgln("TreeTabStore: failed to remove tab {}: {}", session_tab_id, result.error());
}

void TreeTabStore::remove_window(SessionWindowId window_id)
{
    if (!m_database)
        return;

    if (auto result = m_database->try_execute_statement(m_statements.delete_window, {}, window_id); result.is_error())
        dbgln("TreeTabStore: failed to remove window {}: {}", window_id, result.error());
}

}
