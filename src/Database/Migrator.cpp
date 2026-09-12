#include <VoltMod/Core/File.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Database/Database.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace VoltMod
{

struct Migration
{
    int Version;
    std::string Name;
    fs::path Path;
};

/** `[A-Za-z_][A-Za-z0-9_]*` - the table name is interpolated into SQL, so reject anything else. */
static bool IsValidTableName(const std::string& name)
{
    if (name.empty())
        return false;
    if (!std::isalpha(static_cast<unsigned char>(name.front())) && name.front() != '_')
        return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

static std::string HistoryTableDdl(const std::string& table)
{
    return "CREATE TABLE IF NOT EXISTS " + table +
           " (version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at BIGINT NOT NULL)";
}

template <class Conn>
static void ApplyMigration(Conn& conn, const std::string& table, const Migration& migration, const std::string& sql)
{
    for (const std::string& statement : SplitStatements(sql))
        conn(statement);
    // The timestamp is written explicitly: no DEFAULT for it is portable across the drivers.
    conn("INSERT INTO " + table + " (version, name, applied_at) VALUES (" + std::to_string(migration.Version) + ", '" +
         conn.escape(migration.Name) + "', " + std::to_string(Time::Now()) + ")");
}

MigrationResult RunMigrations(Database& db, std::string_view dir, const MigrationOptions& options)
{
    if (!IsValidTableName(options.HistoryTable))
    {
        Log::Error("Invalid migration table name '{}'; refusing to run migrations.", options.HistoryTable);
        return {};
    }
    const std::string& table = options.HistoryTable;

    // Relative paths must resolve against the game dir, not the server process cwd.
    const fs::path resolvedDir = ResolvePath(dir) / DriverName(db.GetDriver());

    std::error_code ec;
    if (!fs::exists(resolvedDir, ec))
    {
        Log::Warn("Migrations directory not found ({}); skipping schema setup.", resolvedDir.string());
        return {.Success = true};
    }

    std::vector<Migration> migrations;
    for (const auto& entry : fs::directory_iterator(resolvedDir, ec))
    {
        if (!entry.is_regular_file())
            continue;
        std::string name = entry.path().filename().string();
        if (!name.ends_with(".sql"))
            continue;
        auto version = ParseMigrationVersion(name);
        if (!version)  // ignore stray files without a leading version (e.g. *.sql.bak)
            continue;
        migrations.push_back({*version, name, entry.path()});
    }
    std::sort(migrations.begin(), migrations.end(),
              [](const Migration& a, const Migration& b) { return a.Version < b.Version; });

    // Runs on the database worker via the blocking Run - load-time only.
    auto outcome = db.Run("migrations", [&](auto& conn) -> MigrationResult {
        using Conn = std::remove_cvref_t<decltype(conn)>;
        const std::string lockKey = std::to_string(options.LockKey);

        conn(HistoryTableDdl(table));

        // Session-level lock held across the per-file transactions below; released when the
        // connection drops. Serializes two plugin loads that race on the same database. SQLite
        // has none: BEGIN IMMEDIATE below takes its single writer lock instead.
        if constexpr (IsPostgres<Conn>)
            conn("SELECT pg_advisory_lock(" + lockKey + ")");
        else if constexpr (IsMariaDb<Conn>)
            conn("DO GET_LOCK('voltmod_migrations_" + lockKey + "', 30)");

        int current = 0;
        for (const auto& row : conn(sqlpp::select(sqlpp::verbatim<sqlpp::integral>("MAX(version)").as(sqlpp::alias::a))
                                        .from(sqlpp::verbatim_table(table))))
            current = static_cast<int>(row.a.value_or(0));

        MigrationResult result{.Success = true, .CurrentVersion = current};
        for (const Migration& m : migrations)
        {
            if (m.Version <= current)
                continue;
            // An unreadable file must not be recorded as applied: an empty statement list would
            // commit the version row having run nothing.
            auto sql = ReadAllText(m.Path.string());
            if (!sql)
            {
                Log::Error("Migration {} ({}) unreadable: {}", m.Version, m.Name, sql.error().Detail);
                result.Success = false;
                break;
            }

            try
            {
                // MariaDB auto-commits every DDL statement, so a failed file there leaves the
                // tables it already created; the version row is still the source of truth.
                if constexpr (IsSqlite<Conn>)
                {
                    conn("BEGIN IMMEDIATE");
                    try
                    {
                        ApplyMigration(conn, table, m, *sql);
                        conn("COMMIT");
                    }
                    catch (...)
                    {
                        conn("ROLLBACK");
                        throw;
                    }
                }
                else
                {
                    auto transaction = sqlpp::start_transaction(conn);
                    ApplyMigration(conn, table, m, *sql);
                    transaction.commit();
                }
                ++result.Applied;
                result.CurrentVersion = m.Version;
                Log::Info("Applied migration {} ({}).", m.Version, m.Name);
            }
            catch (const std::exception& e)
            {
                Log::Error("Migration {} ({}) failed: {}", m.Version, m.Name, e.what());
                result.Success = false;
                break;
            }
        }

        if constexpr (IsPostgres<Conn>)
            conn("SELECT pg_advisory_unlock(" + lockKey + ")");
        else if constexpr (IsMariaDb<Conn>)
            conn("DO RELEASE_LOCK('voltmod_migrations_" + lockKey + "')");

        if (result.Applied > 0)
            Log::Info("Database schema up to date ({} migration(s) applied).", result.Applied);
        return result;
    });

    if (!outcome)
    {
        Log::Error("Migration runner failed: {}", outcome.error().Detail);
        return {};
    }
    return *outcome;
}

}  // namespace VoltMod
