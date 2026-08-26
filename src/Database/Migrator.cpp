#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Database/Migrator.hpp>
#include <VoltMod/Database/PostgresDatabase.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
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

static std::string ReadFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

MigrationResult RunMigrations(PostgresDatabase& db, const std::string& dir, const MigrationOptions& options)
{
    if (!IsValidTableName(options.TableName))
    {
        Log::Error("Invalid migration table name '{}'; refusing to run migrations.", options.TableName);
        return {};
    }
    const std::string& table = options.TableName;

    // Relative paths must resolve against the game dir, not the server process cwd.
    const fs::path resolvedDir = ResolvePath(dir);

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

    // Runs on the database worker via the blocking WithConnection - load-time only.
    // The result fields are filled through these captures (WithConnection blocks).
    int appliedTotal = 0;
    int finalVersion = 0;
    auto outcome = db.WithConnection([&](pqxx::connection& conn) -> bool {
        {
            pqxx::work txn(conn);
            txn.exec("CREATE TABLE IF NOT EXISTS " + table +
                     " (version INTEGER PRIMARY KEY, "
                     "name TEXT NOT NULL, "
                     "applied_at BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM NOW())::BIGINT)");
            txn.commit();
        }

        // Session-level advisory lock held across the per-file transactions below; auto-released if
        // the connection drops. Serializes two plugin loads that race on the same database.
        {
            pqxx::work txn(conn);
            txn.exec("SELECT pg_advisory_lock(" + std::to_string(options.AdvisoryLockKey) + ")");
            txn.commit();
        }

        int current = 0;
        {
            pqxx::work txn(conn);
            pqxx::result r = txn.exec("SELECT COALESCE(MAX(version), 0) FROM " + table);
            current = r[0][0].as<int>();
            txn.commit();
        }
        finalVersion = current;

        int applied = 0;
        bool ok = true;
        for (const Migration& m : migrations)
        {
            if (m.Version <= current)
                continue;
            try
            {
                pqxx::work txn(conn);
                txn.exec(ReadFile(m.Path));
                txn.exec("INSERT INTO " + table + " (version, name) VALUES ($1, $2)", pqxx::params{m.Version, m.Name});
                txn.commit();
                ++applied;
                finalVersion = m.Version;
                Log::Info("Applied migration {} ({}).", m.Version, m.Name);
            }
            catch (const std::exception& e)
            {
                Log::Error("Migration {} ({}) failed: {}", m.Version, m.Name, e.what());
                ok = false;
                break;
            }
        }
        appliedTotal = applied;

        {
            pqxx::work txn(conn);
            txn.exec("SELECT pg_advisory_unlock(" + std::to_string(options.AdvisoryLockKey) + ")");
            txn.commit();
        }

        if (applied > 0)
            Log::Info("Database schema up to date ({} migration(s) applied).", applied);
        return ok;
    });

    if (!outcome)
    {
        Log::Error("Migration runner failed: {}", outcome.error());
        return {.Applied = appliedTotal, .CurrentVersion = finalVersion};
    }
    return {.Success = *outcome, .Applied = appliedTotal, .CurrentVersion = finalVersion};
}

}  // namespace VoltMod
