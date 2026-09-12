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
template <class Conn, class Statement>
int64_t Insert(Conn& conn, const Statement& insert, std::string_view table, std::string_view key = "id")
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

/**
 * Update by a natural key, inserting only when nothing matched: the portable upsert, since the
 * three drivers disagree on `ON CONFLICT` / `ON DUPLICATE KEY`.
 *
 * Not atomic, so the key needs a UNIQUE constraint to turn a race between two servers into one
 * failed job rather than a duplicate row. Relies on `Database` setting CLIENT_FOUND_ROWS, which
 * makes MariaDB count matched rows instead of changed ones.
 */
template <class Conn, class Update, class Statement>
void Upsert(Conn& conn, const Update& update, const Statement& insert)
{
    if (conn(update).affected_rows == 0)
        conn(insert);
}

}  // namespace VoltMod
