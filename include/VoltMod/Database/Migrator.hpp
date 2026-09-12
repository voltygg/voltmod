#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Database/Driver.hpp>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
 * @brief The DDL spellings the three drivers disagree on, one per migration placeholder.
 *
 * The set is closed: anything beyond it belongs in a driver-specific migration.
 */
struct Dialect
{
    std::string_view AutoIncrementKey;  ///< `@ID@`
    std::string_view EpochNow;          ///< `@NOW@`
    std::string_view True;              ///< `@TRUE@`
    std::string_view False;             ///< `@FALSE@`
    std::string_view InsertIfAbsent;    ///< `@INSERT_IF_ABSENT@`
    /** Whether `@ON_CONFLICT(cols)@` renders. Postgres puts it at the end of the statement; the
     *  others carry the same meaning in their INSERT verb. */
    bool NeedsConflictClause;
};

/** The spellings @p driver wants. */
inline Dialect DialectFor(Driver driver)
{
    switch (driver)
    {
    case Driver::Postgres:
        return {"BIGSERIAL PRIMARY KEY", "EXTRACT(EPOCH FROM NOW())::BIGINT", "TRUE", "FALSE", "INSERT INTO", true};
    case Driver::MariaDb:
        return {
            "BIGINT AUTO_INCREMENT PRIMARY KEY", "(UNIX_TIMESTAMP())", "TRUE", "FALSE", "INSERT IGNORE INTO", false};
    case Driver::Sqlite:
        // 1/0 rather than TRUE/FALSE: it is what an existing database's stored schema text says.
        return {
            "INTEGER PRIMARY KEY AUTOINCREMENT", "(strftime('%s','now'))", "1", "0", "INSERT OR IGNORE INTO", false};
    }
    return {};
}

/**
 * Substitute every dialect placeholder in @p sql for @p driver.
 *
 * A placeholder is `@NAME@` or `@NAME(args)@` with NAME in `[A-Z_]`, the same rule the CLI's
 * renderer uses; any other `@`, such as one in an address literal, is ordinary text. An unknown
 * placeholder fails rather than leaving a hole in a statement.
 */
inline Result<std::string> ResolveDialect(std::string_view sql, Driver driver)
{
    static constexpr std::string_view NameChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ_";
    static constexpr std::string_view ConflictToken = "ON_CONFLICT(";
    const Dialect dialect = DialectFor(driver);

    std::string out;
    out.reserve(sql.size());
    size_t copied = 0;
    for (size_t open = sql.find('@'); open != std::string_view::npos; open = sql.find('@', copied))
    {
        size_t close = std::min(sql.find_first_not_of(NameChars, open + 1), sql.size());
        const bool named = close > open + 1;
        if (named && close < sql.size() && sql[close] == '(')
        {
            const size_t args = sql.find_first_of(")@", close);
            if (args != std::string_view::npos && sql[args] == ')')
                close = args + 1;
        }
        if (!named || close >= sql.size() || sql[close] != '@')
        {
            out.append(sql.substr(copied, open + 1 - copied));
            copied = open + 1;
            continue;
        }

        out.append(sql.substr(copied, open - copied));
        copied = close + 1;
        const std::string_view token = sql.substr(open + 1, close - open - 1);
        if (token == "ID")
            out.append(dialect.AutoIncrementKey);
        else if (token == "NOW")
            out.append(dialect.EpochNow);
        else if (token == "TRUE")
            out.append(dialect.True);
        else if (token == "FALSE")
            out.append(dialect.False);
        else if (token == "INSERT_IF_ABSENT")
            out.append(dialect.InsertIfAbsent);
        else if (token.starts_with(ConflictToken))
        {
            if (dialect.NeedsConflictClause)
                out.append("ON CONFLICT (").append(token.substr(ConflictToken.size())).append(" DO NOTHING");
        }
        else
            return std::unexpected(Error::Invalid(std::format("unknown migration placeholder @{}@", token)));
    }
    out.append(sql.substr(copied));
    return out;
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
