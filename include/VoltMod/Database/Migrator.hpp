#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Migration vocabulary only: no pqxx, so a translation unit can parse a migration filename
// without a database client. @ref RunMigrations, which needs a live connection, is declared in
// <VoltMod/Database/PostgresDatabase.hpp>.

namespace VoltMod
{

/** Knobs for @ref RunMigrations; the defaults suit a single plugin owning its database. */
struct MigrationOptions
{
    /** Migration-history table. Must match `[A-Za-z_][A-Za-z0-9_]*` - it is interpolated into SQL. */
    std::string TableName = "schema_migrations";

    /** `pg_advisory_lock` key serializing concurrent loads that share a database. Plugins sharing
     *  one database should use distinct table names AND distinct lock keys. */
    int64_t AdvisoryLockKey = 727274;
};

/** Outcome of @ref RunMigrations. Contextually convertible to bool (success). */
struct MigrationResult
{
    bool Success = false;
    int Applied = 0;         ///< Migrations applied by this run.
    int CurrentVersion = 0;  ///< Max version recorded in the history table after the run.

    explicit operator bool() const { return Success; }
};

/** Leading `NNNN` version of a migration filename, or nullopt when it has none. */
inline std::optional<int> ParseMigrationVersion(std::string_view filename)
{
    int version = 0;
    const char* begin = filename.data();
    const char* end = begin + filename.size();
    auto [ptr, ec] = std::from_chars(begin, end, version);
    if (ec != std::errc{} || ptr == begin)
        return std::nullopt;
    return version;
}

}  // namespace VoltMod
