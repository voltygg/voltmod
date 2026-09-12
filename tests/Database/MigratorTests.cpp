#include <VoltMod/Database/Driver.hpp>
#include <VoltMod/Database/Migrator.hpp>
#include <doctest/doctest.h>
#include <string>

using VoltMod::Driver;
using VoltMod::DriverName;
using VoltMod::ParseDriver;
using VoltMod::ParseMigrationVersion;
using VoltMod::ResolveDialect;
using VoltMod::SplitStatements;

TEST_CASE("ParseMigrationVersion: leading integer")
{
    CHECK_EQ(ParseMigrationVersion("0001_init.sql").value_or(-1), 1);
    CHECK_EQ(ParseMigrationVersion("12_add_bans.sql").value_or(-1), 12);
    CHECK_EQ(ParseMigrationVersion("7.sql").value_or(-1), 7);
}

TEST_CASE("ParseMigrationVersion: no leading version -> nullopt")
{
    CHECK(!ParseMigrationVersion("init.sql").has_value());
    CHECK(!ParseMigrationVersion("").has_value());
    CHECK(!ParseMigrationVersion("_0001_init.sql").has_value());
    CHECK(!ParseMigrationVersion("v2_init.sql").has_value());
}

TEST_CASE("SplitStatements: strips a -- comment")
{
    auto statements = SplitStatements("SELECT 1; -- a comment\nSELECT 2;");
    REQUIRE_EQ(statements.size(), std::size_t{2});
    CHECK_EQ(statements[0], "SELECT 1");
    CHECK_EQ(statements[1], "SELECT 2");
}

TEST_CASE("SplitStatements: a semicolon inside a literal does not split")
{
    auto statements = SplitStatements("INSERT INTO t (a) VALUES ('one;two');");
    REQUIRE_EQ(statements.size(), std::size_t{1});
    CHECK_EQ(statements[0], "INSERT INTO t (a) VALUES ('one;two')");
}

TEST_CASE("SplitStatements: a trailing statement without a semicolon is kept")
{
    auto statements = SplitStatements("SELECT 1;\nSELECT 2");
    REQUIRE_EQ(statements.size(), std::size_t{2});
    CHECK_EQ(statements[1], "SELECT 2");
}

TEST_CASE("SplitStatements: blank input yields no statements")
{
    CHECK(SplitStatements("").empty());
    CHECK(SplitStatements("   \n\t  ").empty());
    CHECK(SplitStatements("-- only a comment\n").empty());
}

TEST_CASE("SplitStatements: a doubled quote inside a literal does not end it early")
{
    auto statements = SplitStatements("INSERT INTO t (a) VALUES ('it''s; fine');");
    REQUIRE_EQ(statements.size(), std::size_t{1});
    CHECK_EQ(statements[0], "INSERT INTO t (a) VALUES ('it''s; fine')");
}

TEST_CASE("ParseDriver and DriverName round trip")
{
    CHECK_EQ(ParseDriver("postgres"), VoltMod::Driver::Postgres);
    CHECK_EQ(ParseDriver("mariadb"), VoltMod::Driver::MariaDb);
    CHECK_EQ(ParseDriver("sqlite"), VoltMod::Driver::Sqlite);
    CHECK_EQ(DriverName(VoltMod::Driver::Postgres), "postgres");
    CHECK_EQ(DriverName(VoltMod::Driver::MariaDb), "mariadb");
    CHECK_EQ(DriverName(VoltMod::Driver::Sqlite), "sqlite");
}

TEST_CASE("ParseDriver: case-insensitive, like every other config enum")
{
    CHECK_EQ(ParseDriver("Postgres"), VoltMod::Driver::Postgres);
    CHECK_EQ(ParseDriver("MariaDB"), VoltMod::Driver::MariaDb);
}

TEST_CASE("ParseDriver: unknown name -> nullopt")
{
    CHECK(!ParseDriver("mysql").has_value());
    CHECK(!ParseDriver("").has_value());
}

TEST_CASE("ResolveDialect: every driver gets its own key, epoch and boolean spellings")
{
    const std::string sql = "id @ID@, at BIGINT DEFAULT @NOW@, ok BOOLEAN DEFAULT @TRUE@, no BOOLEAN DEFAULT @FALSE@";

    CHECK_EQ(ResolveDialect(sql, Driver::Postgres).value_or(""),
             "id BIGSERIAL PRIMARY KEY, at BIGINT DEFAULT EXTRACT(EPOCH FROM NOW())::BIGINT, "
             "ok BOOLEAN DEFAULT TRUE, no BOOLEAN DEFAULT FALSE");
    CHECK_EQ(ResolveDialect(sql, Driver::MariaDb).value_or(""),
             "id BIGINT AUTO_INCREMENT PRIMARY KEY, at BIGINT DEFAULT (UNIX_TIMESTAMP()), "
             "ok BOOLEAN DEFAULT TRUE, no BOOLEAN DEFAULT FALSE");
    CHECK_EQ(ResolveDialect(sql, Driver::Sqlite).value_or(""),
             "id INTEGER PRIMARY KEY AUTOINCREMENT, at BIGINT DEFAULT (strftime('%s','now')), "
             "ok BOOLEAN DEFAULT 1, no BOOLEAN DEFAULT 0");
}

TEST_CASE("ResolveDialect: insert-if-absent is a verb on two drivers and a clause on Postgres")
{
    const std::string sql = "@INSERT_IF_ABSENT@ groups (name) VALUES ('root') @ON_CONFLICT(name)@";

    CHECK_EQ(ResolveDialect(sql, Driver::Postgres).value_or(""),
             "INSERT INTO groups (name) VALUES ('root') ON CONFLICT (name) DO NOTHING");
    CHECK_EQ(ResolveDialect(sql, Driver::MariaDb).value_or(""), "INSERT IGNORE INTO groups (name) VALUES ('root') ");
    CHECK_EQ(ResolveDialect(sql, Driver::Sqlite).value_or(""), "INSERT OR IGNORE INTO groups (name) VALUES ('root') ");
}

TEST_CASE("ResolveDialect: a multi-column conflict target keeps its column list")
{
    CHECK_EQ(ResolveDialect("@ON_CONFLICT(a, b)@", Driver::Postgres).value_or(""), "ON CONFLICT (a, b) DO NOTHING");
}

TEST_CASE("ResolveDialect: an unknown placeholder fails and names itself")
{
    const auto resolved = ResolveDialect("a @ID@ b @MADE_UP@", Driver::Sqlite);
    REQUIRE(!resolved);
    CHECK(resolved.error().Detail.find("@MADE_UP@") != std::string::npos);
}

TEST_CASE("ResolveDialect: an @ that is not placeholder-shaped is ordinary text")
{
    CHECK_EQ(ResolveDialect("VALUES ('someone@example.com'), @TRUE@", Driver::Sqlite).value_or(""),
             "VALUES ('someone@example.com'), 1");
    CHECK_EQ(ResolveDialect("a @ b @ c @(x)@", Driver::Postgres).value_or(""), "a @ b @ c @(x)@");
    CHECK_EQ(ResolveDialect("", Driver::Postgres).value_or("x"), "");
}
