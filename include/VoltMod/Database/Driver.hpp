#pragma once

#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <optional>
#include <string>
#include <string_view>

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
    return Parse<Driver>(name);
}

/** The config spelling of @p driver; also the per-dialect migrations folder name. */
inline std::string DriverName(Driver driver)
{
    return Strings::ToLower(Name(driver));
}

}  // namespace VoltMod
