#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Migration vocabulary only: no database client, so a translation unit can parse a migration
// filename without one. @ref RunMigrations, which needs a live connection, is declared in
// <VoltMod/Database/Database.hpp>.

namespace VoltMod
{

/** Knobs for @ref RunMigrations; the defaults suit a single plugin owning its database. */
struct MigrationOptions
{
    /** Migration-history table. Must match `[A-Za-z_][A-Za-z0-9_]*` - it is interpolated into SQL. */
    std::string HistoryTable = "schema_migrations";

    /** Lock key serializing concurrent loads that share a database (a Postgres advisory lock, a
     *  MariaDB named lock; SQLite relies on its own write lock). Plugins sharing one database
     *  should use distinct table names AND distinct lock keys. */
    int64_t LockKey = 727274;
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

/**
 * Split a migration file into single statements: `--` line comments are stripped, a `;` outside a
 * single-quoted literal ends a statement, and blank statements are dropped. One statement per
 * `;` - procedure bodies are not supported, because MariaDB and SQLite run raw SQL one statement
 * at a time.
 */
inline std::vector<std::string> SplitStatements(std::string_view sql)
{
    static constexpr std::string_view Blanks = " \t\r\n";
    std::vector<std::string> statements;
    std::string current;
    auto flush = [&] {
        const size_t begin = current.find_first_not_of(Blanks);
        if (begin != std::string::npos)
            statements.push_back(current.substr(begin, current.find_last_not_of(Blanks) - begin + 1));
        current.clear();
    };

    bool inLiteral = false;
    for (size_t i = 0; i < sql.size(); ++i)
    {
        const char c = sql[i];
        if (inLiteral)
        {
            // A doubled quote reads as close-then-reopen, which splits the same way.
            inLiteral = c != '\'';
            current.push_back(c);
        }
        else if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-')
        {
            while (i < sql.size() && sql[i] != '\n')
                ++i;
            current.push_back('\n');
        }
        else if (c == ';')
        {
            flush();
        }
        else
        {
            inLiteral = c == '\'';
            current.push_back(c);
        }
    }
    flush();
    return statements;
}

}  // namespace VoltMod
