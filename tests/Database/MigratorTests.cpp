#include <VoltMod/Database/Migrator.hpp>
#include <doctest/doctest.h>

using VoltMod::Database::ParseMigrationVersion;

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
