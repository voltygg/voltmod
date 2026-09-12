#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
// On Windows the MariaDB connector pulls in winsock2.h, so this header must be seen before any
// windows.h an SDK header reaches (see the PCH order in cmake/VoltModPlugin.cmake).
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

/** Insert one row and return its generated id. Postgres reads the `<table>_<key>_seq` sequence
 *  of a SERIAL column; the other drivers report it on the insert result. */
template <class Conn, class Insert>
int64_t InsertReturningId(Conn& conn, const Insert& insert, std::string_view table, std::string_view key = "id")
{
    if constexpr (IsPostgres<Conn>)
    {
        conn(insert);
        return static_cast<int64_t>(conn.last_insert_id(std::string(table), std::string(key)));
    }
    else
    {
        return static_cast<int64_t>(conn(insert).last_insert_id);
    }
}

}  // namespace VoltMod
