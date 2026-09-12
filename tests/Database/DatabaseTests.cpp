#if VOLTMOD_ENABLE_DATABASE

#include <VoltMod/Database/Api.hpp>
#include <doctest/doctest.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <type_traits>

#include "Support/TempPath.hpp"

using VoltMod::Database;
using VoltMod::DbResult;
using VoltMod::Driver;
using VoltMod::RunMigrations;
using VoltMod::Scheduler;

namespace
{

// A small table spec, shaped the way a plugin's own table header would declare one.
struct T_
{
    VOLTMOD_COLUMN(id, id, sqlpp::integral, std::true_type);
    VOLTMOD_COLUMN(name, name, sqlpp::text, std::false_type);

    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(t, t);
    template <typename Table>
    using _table_columns = sqlpp::table_columns<Table, id, name>;
    using _required_insert_columns = sqlpp::detail::type_set<sqlpp::column_t<sqlpp::table_t<T_>, name>>;
};
using T = sqlpp::table_t<T_>;

}  // namespace

TEST_CASE("Database: Start against in-memory sqlite succeeds and connects")
{
    Scheduler scheduler;
    Database db(scheduler);

    REQUIRE(db.Start({.driver = "sqlite", .path = ":memory:"}));
    CHECK_EQ(db.GetDriver(), Driver::Sqlite);
    CHECK(db.IsConnected());
}

TEST_CASE("Database: Start fails on an unknown driver")
{
    Scheduler scheduler;
    Database db(scheduler);

    CHECK(!db.Start({.driver = "mysql"}));
}

TEST_CASE("Database: Start fails on sqlite with an empty path")
{
    Scheduler scheduler;
    Database db(scheduler);

    CHECK(!db.Start({.driver = "sqlite", .path = ""}));
}

TEST_CASE("Database: RunBlocking runs a raw create, a typed insert, and a typed select")
{
    Scheduler scheduler;
    Database db(scheduler);
    REQUIRE(db.Start({.driver = "sqlite", .path = ":memory:"}));

    auto created = db.RunBlocking(
        "create-table", [](auto& conn) { conn("CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL)"); });
    REQUIRE(created.has_value());

    const T t;
    auto insertedId = db.RunBlocking("insert-row", [&](auto& conn) {
        return VoltMod::InsertReturningId(conn, sqlpp::insert_into(t).set(t.name = "widget"), "t");
    });
    REQUIRE(insertedId.has_value());
    CHECK_EQ(*insertedId, 1);

    auto name = db.RunBlocking("select-row", [&](auto& conn) -> std::string {
        std::string found;
        for (const auto& row : conn(sqlpp::select(sqlpp::all_of(t)).from(t).where(t.id == 1)))
            found = row.name;
        return found;
    });
    REQUIRE(name.has_value());
    CHECK_EQ(*name, "widget");
}

TEST_CASE("Database: Run delivers its result only through DispatchCompletions")
{
    Scheduler scheduler;
    Database db(scheduler);
    REQUIRE(db.Start({.driver = "sqlite", .path = ":memory:"}));

    bool done = false;
    DbResult<int> result = std::unexpected("never ran");
    db.Run(
        "ping-async",
        [](auto& conn) -> int {
            for (const auto& row : conn(sqlpp::select(sqlpp::value(1).as(sqlpp::alias::a))))
                (void)row;
            return 1;
        },
        [&](DbResult<int> r) {
            done = true;
            result = std::move(r);
        });

    // Start registered per-frame delivery; only OnGameFrame() dispatches queued completions.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline)
    {
        scheduler.OnGameFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(done);
    REQUIRE(result.has_value());
    CHECK_EQ(*result, 1);
}

TEST_CASE("Database: a job that throws yields an error, the next job still succeeds, and the connection reopens")
{
    Scheduler scheduler;
    Database db(scheduler);
    VoltModTests::TempFile file("", "database-reopen", ".sqlite3");
    REQUIRE(db.Start({.driver = "sqlite", .path = file.Path()}));

    auto created = db.RunBlocking("create-table", [](auto& conn) { conn("CREATE TABLE t (id INTEGER PRIMARY KEY)"); });
    REQUIRE(created.has_value());

    auto failed = db.RunBlocking("select-missing", [](auto& conn) -> bool {
        conn("SELECT * FROM this_table_does_not_exist");
        return true;
    });
    CHECK(!failed.has_value());

    // The connection dropped and reopens here; the table created before the failure survives it.
    auto rowCount = db.RunBlocking("count-rows", [](auto& conn) {
        int count = 0;
        for (const auto& row :
             conn(sqlpp::select(sqlpp::verbatim<sqlpp::integral>("COUNT(*)").as(sqlpp::alias::a)).from(sqlpp::verbatim_table("t"))))
            count = static_cast<int>(row.a.value_or(0));
        return count;
    });
    REQUIRE(rowCount.has_value());
    CHECK_EQ(*rowCount, 0);
}

TEST_CASE("Database: Run after Stop delivers an error only on the next DispatchCompletions")
{
    Scheduler scheduler;
    Database db(scheduler);
    REQUIRE(db.Start({.driver = "sqlite", .path = ":memory:"}));
    db.Stop();

    bool done = false;
    DbResult<int> result = std::unexpected("never ran");
    db.Run(
        "after-stop", [](auto&) -> int { return 1; },
        [&](DbResult<int> r) {
            done = true;
            result = std::move(r);
        });
    CHECK(!done);  // Stop tore down per-frame delivery; nothing runs it inline either

    db.DispatchCompletions();
    REQUIRE(done);
    REQUIRE(!result.has_value());
    CHECK_EQ(result.error(), "database not running");
}

TEST_CASE("RunMigrations: applies migrations in order, is idempotent, and stops on a bad file")
{
    Scheduler scheduler;
    Database db(scheduler);
    REQUIRE(db.Start({.driver = "sqlite", .path = ":memory:"}));

    VoltModTests::TempDir dir("run-migrations");
    std::filesystem::create_directories(std::filesystem::path(dir.Path()) / "sqlite");
    dir.Write("sqlite/0001_a.sql",
              "CREATE TABLE a (id INTEGER PRIMARY KEY, note TEXT NOT NULL DEFAULT 'x;y'); -- seed table\n"
              "INSERT INTO a (note) VALUES ('seed');\n");
    dir.Write("sqlite/0002_b.sql", "CREATE TABLE b (id INTEGER PRIMARY KEY);\n");

    auto first = RunMigrations(db, dir.Path());
    CHECK(first.Success);
    CHECK_EQ(first.Applied, 2);
    CHECK_EQ(first.CurrentVersion, 2);

    auto tableExists = [&](const std::string& name) {
        auto found = db.RunBlocking("table-exists", [&](auto& conn) {
            int count = 0;
            for (const auto& row : conn(sqlpp::select(sqlpp::verbatim<sqlpp::integral>("COUNT(*)").as(sqlpp::alias::a))
                                             .from(sqlpp::verbatim_table("sqlite_master"))
                                             .where(sqlpp::verbatim<sqlpp::boolean>("name = '" + name + "'"))))
                count = static_cast<int>(row.a.value_or(0));
            return count;
        });
        REQUIRE(found.has_value());
        return *found > 0;
    };
    CHECK(tableExists("a"));
    CHECK(tableExists("b"));

    auto rerun = RunMigrations(db, dir.Path());
    CHECK(rerun.Success);
    CHECK_EQ(rerun.Applied, 0);
    CHECK_EQ(rerun.CurrentVersion, 2);

    dir.Write("sqlite/0003_c.sql", "CREATE TBLE typo (id INTEGER);\n");
    auto bad = RunMigrations(db, dir.Path());
    CHECK(!bad.Success);
    CHECK_EQ(bad.CurrentVersion, 2);
    CHECK(!tableExists("typo"));

    auto versionThreeRow = db.RunBlocking("version-three-row", [](auto& conn) {
        int count = 0;
        for (const auto& row : conn(sqlpp::select(sqlpp::verbatim<sqlpp::integral>("COUNT(*)").as(sqlpp::alias::a))
                                         .from(sqlpp::verbatim_table("schema_migrations"))
                                         .where(sqlpp::verbatim<sqlpp::boolean>("version = 3"))))
            count = static_cast<int>(row.a.value_or(0));
        return count;
    });
    REQUIRE(versionThreeRow.has_value());
    CHECK_EQ(*versionThreeRow, 0);
}

TEST_CASE("RunMigrations: a missing driver folder is a successful no-op")
{
    Scheduler scheduler;
    Database db(scheduler);
    REQUIRE(db.Start({.driver = "sqlite", .path = ":memory:"}));

    VoltModTests::TempDir dir("run-migrations-missing");
    auto result = RunMigrations(db, dir.Path());
    CHECK(result.Success);
    CHECK_EQ(result.Applied, 0);
}

#endif  // VOLTMOD_ENABLE_DATABASE
