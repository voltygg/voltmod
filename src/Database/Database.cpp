#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Database/Database.hpp>
#include <filesystem>
#include <optional>

namespace VoltMod
{

using PostgresSslMode = sqlpp::postgresql::connection_config::sslmode_t;

static std::optional<PostgresSslMode> ParseSslMode(const std::string& mode)
{
    if (mode == "disable")
        return PostgresSslMode::disable;
    if (mode == "allow")
        return PostgresSslMode::allow;
    if (mode == "prefer")
        return PostgresSslMode::prefer;
    if (mode == "require")
        return PostgresSslMode::require;
    if (mode == "verify-ca")
        return PostgresSslMode::verify_ca;
    if (mode == "verify-full")
        return PostgresSslMode::verify_full;
    return std::nullopt;
}

static bool WantsTls(const std::string& sslMode)
{
    return sslMode == "require" || sslMode == "verify-ca" || sslMode == "verify-full";
}

static sqlpp::postgresql::connection_config PostgresSettings(const DatabaseConfig& config)
{
    sqlpp::postgresql::connection_config settings;
    settings.host = config.host;
    settings.port = config.port > 0 ? static_cast<uint32_t>(config.port) : 5432u;
    settings.dbname = config.database;
    settings.user = config.username;
    settings.password = config.password;
    settings.connect_timeout = static_cast<uint32_t>(config.connectTimeoutSec);
    settings.sslmode = ParseSslMode(config.sslMode).value_or(PostgresSslMode::prefer);
    return settings;
}

static sqlpp::mysql::connection_config MariaDbSettings(const DatabaseConfig& config)
{
    sqlpp::mysql::connection_config settings;
    settings.host = config.host;
    settings.port = config.port > 0 ? static_cast<unsigned int>(config.port) : 3306u;
    settings.database = config.database;
    settings.user = config.username;
    settings.password = config.password;
    settings.connect_timeout_seconds = static_cast<unsigned int>(config.connectTimeoutSec);
    settings.charset = "utf8mb4";
    settings.ssl = WantsTls(config.sslMode);
    // Affected rows must mean matched rows, which update-then-insert reads to decide on the insert.
    settings.client_flag |= CLIENT_FOUND_ROWS;
    return settings;
}

static sqlpp::sqlite3::connection_config SqliteSettings(const DatabaseConfig& config)
{
    sqlpp::sqlite3::connection_config settings;
    settings.flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (config.path == ":memory:")
    {
        settings.path_to_database = config.path;
        return settings;
    }

    // Relative paths must resolve against the game dir, not the server process cwd.
    const std::filesystem::path file = ResolvePath(config.path);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    settings.path_to_database = file.string();
    return settings;
}

static bool ConfigIsValid(Driver driver, const DatabaseConfig& config)
{
    if (driver == Driver::Sqlite && config.path.empty())
    {
        Log::Error("Database driver 'sqlite' needs a 'path' (a file, or \":memory:\").");
        return false;
    }
    if (driver == Driver::Postgres && !ParseSslMode(config.sslMode))
    {
        Log::Error("Unknown sslMode '{}'; expected disable, allow, prefer, require, verify-ca or verify-full.",
                   config.sslMode);
        return false;
    }
    return true;
}

Database::~Database()
{
    Stop();
}

bool Database::Start(const DatabaseConfig& config)
{
    auto driver = ParseDriver(config.driver);
    if (!driver)
    {
        Log::Error("Unknown database driver '{}'; expected postgres, mariadb or sqlite.", config.driver);
        return false;
    }
    if (!ConfigIsValid(*driver, config))
        return false;

    {
        std::lock_guard lock(_queueMutex);
        if (_worker.joinable())
            return true;  // already started
        _config = config;
        _driver = *driver;
        _accepting = true;
        _stopping = false;
    }

    _worker = std::thread([this] { WorkerMain(); });

    // Verify connectivity up front so the plugin can degrade instead of queueing into the void.
    // Typed, not raw: a raw SELECT leaves an unread result set on MariaDB.
    auto ping = RunBlocking("db_ping", [](auto& conn) {
        for (const auto& row : conn(sqlpp::select(sqlpp::value(1).as(sqlpp::alias::a))))
            (void)row;
    });
    if (!ping)
    {
        Stop();
        return false;
    }

    _onFrame = _scheduler.EveryFrame([this] { DispatchCompletions(); });
    return true;
}

void Database::Stop(std::chrono::milliseconds stopDeadline)
{
    {
        std::lock_guard lock(_queueMutex);
        if (!_worker.joinable() && !_accepting)
            return;
        _accepting = false;
        _stopping = true;
        _stopDeadline = std::chrono::steady_clock::now() + stopDeadline;
    }
    _queueCv.notify_all();

    if (_worker.joinable())
        _worker.join();

    _onFrame.Reset();

    // Undispatched completions are destroyed unrun: the engine/plugin state they would touch
    // is going away with this unload.
    {
        std::lock_guard lock(_completionMutex);
        _completions.clear();
    }
}

void Database::DispatchCompletions()
{
    std::vector<std::move_only_function<void()>> ready;
    {
        std::lock_guard lock(_completionMutex);
        ready.swap(_completions);
    }
    for (auto& completion : ready)
        completion();
}

void Database::PushCompletion(std::move_only_function<void()> completion)
{
    std::lock_guard lock(_completionMutex);
    _completions.push_back(std::move(completion));
}

void Database::Enqueue(Job job)
{
    bool accepted = false;
    {
        std::lock_guard lock(_queueMutex);
        accepted = _accepting;
        if (accepted)
            _queue.push_back(std::move(job));
    }

    if (accepted)
    {
        _queueCv.notify_all();
        return;
    }

    Log::Warn("db: '{}' failed - database not running.", job.Name);

    // Async failures are queued for the next dispatch rather than invoked here. Every other
    // completion reaches the caller on a later frame, and a caller that is mid-iteration over its
    // own container when it enqueues must not be re-entered on this stack.
    job.Fail("database not running");
}

void Database::WorkerMain()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock lock(_queueMutex);
            _queueCv.wait(lock, [&] { return !_queue.empty() || _stopping; });

            if (_queue.empty() && _stopping)
                break;

            // Past the stop deadline: drop what's left (a dead database must not hang unload).
            if (_stopping && std::chrono::steady_clock::now() >= _stopDeadline)
            {
                for (auto& dropped : _queue)
                {
                    Log::Warn("db: dropping queued '{}' - shutdown stop deadline reached.", dropped.Name);
                    dropped.Fail("shutdown");
                }
                _queue.clear();
                break;
            }

            job = std::move(_queue.front());
            _queue.pop_front();
        }

        if (!EnsureOpen())
        {
            Log::Error("db: '{}' failed - no database connection.", job.Name);
            job.Fail("no database connection");
            continue;
        }

        try
        {
            job.Body(_connection);
        }
        catch (const std::exception& e)
        {
            Log::Error("db: {} failed: {}", job.Name, e.what());
            DropConnection();  // the connection state is unknown; reopen on the next job
            job.Fail(e.what());
        }
    }

    DropConnection();
}

bool Database::EnsureOpen()
{
    const bool open = std::visit(
        [](const auto& conn) {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(conn)>, std::monostate>)
                return false;
            else
                return conn.is_connected();
        },
        _connection);
    if (open)
        return true;

    try
    {
        switch (_driver)
        {
        case Driver::Postgres:
            _connection.emplace<PostgresConnection>(PostgresSettings(_config));
            break;
        case Driver::MariaDb:
            sqlpp::mysql::global_library_init();
            _connection.emplace<MariaDbConnection>(MariaDbSettings(_config));
            break;
        case Driver::Sqlite:
        {
            auto& conn = _connection.emplace<SqliteConnection>(SqliteSettings(_config));
            if (_config.path != ":memory:")
                conn("PRAGMA journal_mode=WAL");
            conn("PRAGMA busy_timeout=" + std::to_string(_config.connectTimeoutSec * 1000));
            break;
        }
        }
        _connected.store(true, std::memory_order_relaxed);
        return true;
    }
    catch (const std::exception&)
    {
        // Don't log the exception text: a failed connect can echo the full connection string
        // (password included). Report a generic, secret-free message instead.
        Log::Error("Database connection failed - check host/port/credentials.");
    }
    DropConnection();
    return false;
}

void Database::DropConnection()
{
    _connection.emplace<std::monostate>();
    _connected.store(false, std::memory_order_relaxed);
}

}  // namespace VoltMod
