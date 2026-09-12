#pragma once

// On Windows the MariaDB connector pulls in winsock2.h, so this header must be seen before any
// windows.h an SDK header reaches (see the PCH order in cmake/VoltModPlugin.cmake).
#include <VoltMod/Database/Driver.hpp>
#include <concepts>
#include <sqlpp23/mysql/mysql.h>
#include <sqlpp23/postgresql/postgresql.h>
#include <sqlpp23/sqlite3/sqlite3.h>
#include <sqlpp23/sqlpp23.h>
#include <type_traits>

namespace VoltMod
{

using PostgresConnection = sqlpp::postgresql::connection;
using MariaDbConnection = sqlpp::mysql::connection;
using SqliteConnection = sqlpp::sqlite3::connection;

/** Dialect tests for the `if constexpr` branches of a job body run against `auto& conn`. */
template <class Conn>
inline constexpr bool IsPostgres = std::same_as<std::remove_cvref_t<Conn>, PostgresConnection>;

template <class Conn>
inline constexpr bool IsMariaDb = std::same_as<std::remove_cvref_t<Conn>, MariaDbConnection>;

template <class Conn>
inline constexpr bool IsSqlite = std::same_as<std::remove_cvref_t<Conn>, SqliteConnection>;

template <class Conn>
    requires(IsPostgres<Conn> || IsMariaDb<Conn> || IsSqlite<Conn>)
inline constexpr Driver DriverOf = IsPostgres<Conn>  ? Driver::Postgres
                                   : IsMariaDb<Conn> ? Driver::MariaDb
                                                     : Driver::Sqlite;

}  // namespace VoltMod
