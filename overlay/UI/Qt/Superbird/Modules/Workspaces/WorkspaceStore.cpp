/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <AK/Format.h>
#include <AK/OwnPtr.h>
#include <LibDatabase/ResultRow.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceStore.h>

namespace Superbird {

static constexpr auto WORKSPACE_STORE_NAME = "Workspaces"sv;
static constexpr size_t MAXIMUM_TEXT_BYTES = 1024;

WorkspaceStore::WorkspaceStore(NonnullRefPtr<Database::Database> database, Statements statements)
    : m_database(move(database))
    , m_statements(statements)
{
}

NonnullOwnPtr<WorkspaceStore> WorkspaceStore::create_transient()
{
    return adopt_own(*new WorkspaceStore());
}

ErrorOr<NonnullOwnPtr<WorkspaceStore>> WorkspaceStore::create(ByteString const& data_directory)
{
    auto database = TRY(Database::Database::create(data_directory, WORKSPACE_STORE_NAME));
    TRY(database->set_busy_timeout(2000));

    auto migrations = to_array<Database::Migration>({
        {
            .version = 1,
            .sql = R"#(
                CREATE TABLE IF NOT EXISTS Workspaces (
                    session_window_id INTEGER NOT NULL,
                    workspace_id INTEGER NOT NULL,
                    name TEXT NOT NULL,
                    icon TEXT NOT NULL DEFAULT '',
                    tint INTEGER,
                    position INTEGER NOT NULL,
                    is_active INTEGER NOT NULL DEFAULT 0,
                    PRIMARY KEY (session_window_id, workspace_id)
                );
                CREATE TABLE IF NOT EXISTS WorkspaceTabs (
                    session_tab_id INTEGER PRIMARY KEY,
                    session_window_id INTEGER NOT NULL,
                    workspace_id INTEGER NOT NULL
                );
                CREATE INDEX IF NOT EXISTS idx_workspace_tabs_window
                    ON WorkspaceTabs(session_window_id);
            )#",
            .backfill = {},
        },
    });

    if (TRY(database->migrate(WORKSPACE_STORE_NAME, migrations)) != Database::MigrationOutcome::Success)
        return Error::from_string_literal("Workspaces database was created by a newer Superbird version");

    Statements statements {};
    statements.select_workspaces = TRY(database->prepare_statement(
        "SELECT workspace_id, name, icon, tint, position, is_active FROM Workspaces WHERE session_window_id = :session_window_id ORDER BY position;"sv));
    statements.select_assignments = TRY(database->prepare_statement(
        "SELECT session_tab_id, workspace_id FROM WorkspaceTabs WHERE session_window_id = :session_window_id;"sv));
    statements.delete_workspaces = TRY(database->prepare_statement("DELETE FROM Workspaces WHERE session_window_id = :session_window_id;"sv));
    statements.delete_assignments = TRY(database->prepare_statement("DELETE FROM WorkspaceTabs WHERE session_window_id = :session_window_id;"sv));
    statements.insert_workspace = TRY(database->prepare_statement(
        "INSERT INTO Workspaces (session_window_id, workspace_id, name, icon, tint, position, is_active) "
        "VALUES (:session_window_id, :workspace_id, :name, :icon, :tint, :position, :is_active);"sv));
    // NB: A tab that moved to another window takes its row along.
    statements.upsert_assignment = TRY(database->prepare_statement(
        "INSERT INTO WorkspaceTabs (session_tab_id, session_window_id, workspace_id) VALUES (:session_tab_id, :session_window_id, :workspace_id) "
        "ON CONFLICT(session_tab_id) DO UPDATE SET session_window_id = excluded.session_window_id, workspace_id = excluded.workspace_id;"sv));

    return adopt_own(*new WorkspaceStore(move(database), statements));
}

WorkspaceStore& WorkspaceStore::the(bool is_private)
{
    static OwnPtr<WorkspaceStore> persistent;
    static OwnPtr<WorkspaceStore> transient;
    if (is_private) {
        if (!transient)
            transient = create_transient();
        return *transient;
    }
    if (!persistent) {
        auto const& data_directory = WindowHost::persistent_data_directory();
        if (!data_directory.has_value()) {
            persistent = create_transient();
            return *persistent;
        }
        auto store = create(*data_directory);
        if (store.is_error()) {
            dbgln("Workspaces will not be persisted this session: {}", store.error());
            persistent = create_transient();
        } else {
            persistent = store.release_value();
        }
    }
    return *persistent;
}

static std::string to_std_string(String const& string)
{
    auto view = string.bytes_as_string_view();
    return std::string(view.characters_without_null_termination(), view.length());
}

static ByteString to_byte_string(std::string const& string)
{
    return ByteString(string.data(), string.size());
}

Workspaces::WorkspaceRows WorkspaceStore::load_window(Workspaces::WindowKey window)
{
    if (!m_database) {
        Workspaces::WorkspaceRows rows;
        for (auto const& row : m_memory.workspaces) {
            if (row.window == window)
                rows.workspaces.push_back(row);
        }
        for (auto const& row : m_memory.assignments) {
            if (row.window == window)
                rows.assignments.push_back(row);
        }
        return rows;
    }

    Workspaces::WorkspaceRows rows;
    auto bind_window = [&](auto& bind) -> ErrorOr<void> { return bind("session_window_id"sv, i64 { window }); };

    auto workspaces_result = m_database->try_execute_bound_statement(m_statements.select_workspaces, bind_window, [&](Database::ResultRow& row) -> ErrorOr<void> {
        auto tint = TRY(row.read_optional_integer<i64>("tint"sv));
        rows.workspaces.push_back({
            .window = window,
            .id = TRY(row.read_integer<i64>("workspace_id"sv)),
            .name = to_std_string(TRY(row.read_text("name"sv, MAXIMUM_TEXT_BYTES))),
            .icon = to_std_string(TRY(row.read_text("icon"sv, MAXIMUM_TEXT_BYTES))),
            .tint = tint.has_value() ? std::optional<uint32_t>(static_cast<uint32_t>(*tint & 0xFFFFFF)) : std::nullopt,
            .position = TRY(row.read_integer<i64>("position"sv)),
            .is_active = TRY(row.read_bool("is_active"sv)),
        });
        return {};
    });
    if (workspaces_result.is_error())
        dbgln("WorkspaceStore: failed to load workspaces of window {}: {}", window, workspaces_result.error());

    auto assignments_result = m_database->try_execute_bound_statement(m_statements.select_assignments, bind_window, [&](Database::ResultRow& row) -> ErrorOr<void> {
        rows.assignments.push_back({
            .tab = TRY(row.read_integer<i64>("session_tab_id"sv)),
            .window = window,
            .workspace = TRY(row.read_integer<i64>("workspace_id"sv)),
        });
        return {};
    });
    if (assignments_result.is_error())
        dbgln("WorkspaceStore: failed to load tab assignments of window {}: {}", window, assignments_result.error());

    return rows;
}

void WorkspaceStore::save_window(Workspaces::WindowKey window, Workspaces::WorkspaceRows const& rows)
{
    if (!m_database) {
        std::erase_if(m_memory.workspaces, [&](auto const& row) { return row.window == window; });
        std::erase_if(m_memory.assignments, [&](auto const& row) { return row.window == window; });
        m_memory.workspaces.insert(m_memory.workspaces.end(), rows.workspaces.begin(), rows.workspaces.end());
        m_memory.assignments.insert(m_memory.assignments.end(), rows.assignments.begin(), rows.assignments.end());
        return;
    }

    auto result = m_database->transaction([&]() -> ErrorOr<void> {
        auto bind_window = [&](auto& bind) -> ErrorOr<void> { return bind("session_window_id"sv, i64 { window }); };
        TRY(m_database->try_execute_bound_statement(m_statements.delete_workspaces, bind_window));
        TRY(m_database->try_execute_bound_statement(m_statements.delete_assignments, bind_window));
        for (auto const& row : rows.workspaces) {
            TRY(m_database->try_execute_bound_statement(m_statements.insert_workspace, [&](auto& bind) -> ErrorOr<void> {
                TRY(bind("session_window_id"sv, i64 { window }));
                TRY(bind("workspace_id"sv, i64 { row.id }));
                TRY(bind("name"sv, to_byte_string(row.name)));
                TRY(bind("icon"sv, to_byte_string(row.icon)));
                TRY(bind("tint"sv, row.tint.has_value() ? Optional<i64> { static_cast<i64>(*row.tint) } : Optional<i64> {}));
                TRY(bind("position"sv, i64 { row.position }));
                TRY(bind("is_active"sv, i64 { row.is_active ? 1 : 0 }));
                return {};
            }));
        }
        for (auto const& row : rows.assignments) {
            TRY(m_database->try_execute_bound_statement(m_statements.upsert_assignment, [&](auto& bind) -> ErrorOr<void> {
                TRY(bind("session_tab_id"sv, i64 { row.tab }));
                TRY(bind("session_window_id"sv, i64 { window }));
                TRY(bind("workspace_id"sv, i64 { row.workspace }));
                return {};
            }));
        }
        return {};
    });
    if (result.is_error())
        dbgln("WorkspaceStore: failed to save workspaces of window {}: {}", window, result.error());
}

}
