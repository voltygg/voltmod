#pragma once

#include <optional>
#include <string_view>
#include <utility>

namespace VoltMod
{

/** Database backend selected at runtime by @ref DatabaseConfig::driver. */
enum class Driver
{
    Postgres,
    MariaDb,
    Sqlite
};

/** Parse a config `driver` value ("postgres", "mariadb", "sqlite"); nullopt for anything else. */
inline std::optional<Driver> ParseDriver(std::string_view name)
{
    if (name == "postgres")
        return Driver::Postgres;
    if (name == "mariadb")
        return Driver::MariaDb;
    if (name == "sqlite")
        return Driver::Sqlite;
    return std::nullopt;
}

/** The config spelling of @p driver; also the per-dialect migrations folder name. */
inline std::string_view DriverName(Driver driver)
{
    switch (driver)
    {
    case Driver::Postgres:
        return "postgres";
    case Driver::MariaDb:
        return "mariadb";
    case Driver::Sqlite:
        return "sqlite";
    }
    std::unreachable();
}

}  // namespace VoltMod
