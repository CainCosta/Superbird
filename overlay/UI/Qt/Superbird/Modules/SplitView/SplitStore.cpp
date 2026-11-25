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
#include <UI/Qt/Superbird/Modules/SplitView/SplitStore.h>

namespace Superbird {

static constexpr auto SPLIT_STORE_NAME = "SplitView"sv;
static constexpr size_t MAXIMUM_LAYOUT_BYTES = 512;

SplitStore::SplitStore(NonnullRefPtr<Database::Database> database, Statements statements)
    : m_database(move(database))
    , m_statements(statements)
{
}

NonnullOwnPtr<SplitStore> SplitStore::create_transient()
{
    return adopt_own(*new SplitStore());
}

ErrorOr<NonnullOwnPtr<SplitStore>> SplitStore::create(ByteString const& data_directory)
{
    auto database = TRY(Database::Database::create(data_directory, SPLIT_STORE_NAME));
    TRY(database->set_busy_timeout(2000));

    auto migrations = to_array<Database::Migration>({
        {
            .version = 1,
            .sql = R"#(
                CREATE TABLE IF NOT EXISTS SplitGroups (
                    session_window_id INTEGER PRIMARY KEY,
                    layout TEXT NOT NULL
                );
            )#",
            .backfill = {},
        },
    });
    if (TRY(database->migrate(SPLIT_STORE_NAME, migrations)) != Database::MigrationOutcome::Success)
        return Error::from_string_literal("Split View database was created by a newer Superbird version");

    Statements statements {};
    statements.select_layout = TRY(database->prepare_statement("SELECT layout FROM SplitGroups WHERE session_window_id = :session_window_id;"sv));
    statements.upsert_layout = TRY(database->prepare_statement(
        "INSERT INTO SplitGroups (session_window_id, layout) VALUES (:session_window_id, :layout) "
        "ON CONFLICT(session_window_id) DO UPDATE SET layout = excluded.layout;"sv));
    statements.delete_layout = TRY(database->prepare_statement("DELETE FROM SplitGroups WHERE session_window_id = :session_window_id;"sv));
    return adopt_own(*new SplitStore(move(database), statements));
}

SplitStore& SplitStore::the(bool is_private)
{
    static OwnPtr<SplitStore> persistent;
    static OwnPtr<SplitStore> transient;
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
            dbgln("Split View groups will not be persisted this session: {}", store.error());
            persistent = create_transient();
        } else {
            persistent = store.release_value();
        }
    }
    return *persistent;
}

Optional<ByteString> SplitStore::load_window(i64 window)
{
    if (!m_database) {
        if (auto layout = m_memory.get(window); layout.has_value())
            return ByteString(*layout);
        return {};
    }

    Optional<ByteString> layout;
    auto result = m_database->try_execute_bound_statement(
        m_statements.select_layout,
        [&](auto& bind) -> ErrorOr<void> { return bind("session_window_id"sv, window); },
        [&](Database::ResultRow& row) -> ErrorOr<void> {
            layout = TRY(row.read_text("layout"sv, MAXIMUM_LAYOUT_BYTES)).to_byte_string();
            return {};
        });
    if (result.is_error())
        dbgln("SplitStore: failed to load the split of window {}: {}", window, result.error());
    return layout;
}

void SplitStore::save_window(i64 window, Optional<ByteString> const& layout)
{
    if (!m_database) {
        if (layout.has_value())
            m_memory.set(window, *layout);
        else
            m_memory.remove(window);
        return;
    }

    ErrorOr<void> result;
    if (layout.has_value()) {
        result = m_database->try_execute_bound_statement(m_statements.upsert_layout, [&](auto& bind) -> ErrorOr<void> {
            TRY(bind("session_window_id"sv, window));
            TRY(bind("layout"sv, *layout));
            return {};
        });
    } else {
        result = m_database->try_execute_bound_statement(m_statements.delete_layout, [&](auto& bind) -> ErrorOr<void> {
            return bind("session_window_id"sv, window);
        });
    }
    if (result.is_error())
        dbgln("SplitStore: failed to save the split of window {}: {}", window, result.error());
}

}
