#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Database/PostgresDatabase.hpp>
#include <format>

namespace VoltMod
{

std::string PostgresConfig::GetConnectionString() const
{
    return std::format("host={} port={} dbname={} user={} password={} sslmode={} connect_timeout={}", host, port,
                       database, username, password, sslMode, connectTimeoutSec);
}

PostgresDatabase::~PostgresDatabase()
{
    Stop();
}

bool PostgresDatabase::Start(const PostgresConfig& config)
{
    {
        std::lock_guard lock(_queueMutex);
        if (_worker.joinable())
            return true;  // already started
        _connectionString = config.GetConnectionString();
        _accepting = true;
        _stopping = false;
    }

    _worker = std::thread([this] { WorkerMain(); });

    // Verify connectivity up front so the plugin can degrade instead of queueing into the void.
    if (!QueryBlocking("db_ping", "SELECT 1"))
    {
        Stop();
        return false;
    }

    _onFrame = _scheduler.EveryFrame([this] { DispatchCompletions(); });
    return true;
}

void PostgresDatabase::Stop(std::chrono::milliseconds stopDeadline)
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

void PostgresDatabase::Query(std::string name, std::string sql, pqxx::params params, ResultCallback onDone)
{
    Enqueue({.Name = std::move(name), .Sql = std::move(sql), .Params = std::move(params), .OnDone = std::move(onDone)});
}

void PostgresDatabase::Exec(std::string name, std::string sql, pqxx::params params)
{
    // No completion: the worker already logs failures with the job name.
    Enqueue({.Name = std::move(name), .Sql = std::move(sql), .Params = std::move(params)});
}

DbResult<pqxx::result> PostgresDatabase::QueryBlocking(const std::string& name, const std::string& sql,
                                                       pqxx::params params)
{
    auto waiter = std::make_shared<Waiter>();
    {
        std::lock_guard lock(_queueMutex);
        if (!_accepting)
            return std::unexpected(std::string("database not running"));
        _queue.push_back({.Name = name, .Sql = sql, .Params = std::move(params), .Wait = waiter});
    }
    _queueCv.notify_all();

    std::unique_lock lock(waiter->M);
    waiter->Cv.wait(lock, [&] { return waiter->Done; });
    return std::move(waiter->Result);
}

DbResult<bool> PostgresDatabase::WithConnection(std::function<bool(pqxx::connection&)> fn)
{
    auto waiter = std::make_shared<Waiter>();
    {
        std::lock_guard lock(_queueMutex);
        if (!_accepting)
            return std::unexpected(std::string("database not running"));
        _queue.push_back({.Name = "with_connection", .Raw = std::move(fn), .Wait = waiter});
    }
    _queueCv.notify_all();

    std::unique_lock lock(waiter->M);
    waiter->Cv.wait(lock, [&] { return waiter->Done; });
    if (!waiter->Result)
        return std::unexpected(waiter->Result.error());
    return waiter->RawOk;
}

void PostgresDatabase::DispatchCompletions()
{
    std::vector<std::pair<ResultCallback, DbResult<pqxx::result>>> ready;
    {
        std::lock_guard lock(_completionMutex);
        ready.swap(_completions);
    }
    for (auto& [callback, result] : ready)
        callback(std::move(result));
}

void PostgresDatabase::Enqueue(Job job)
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

    // Query callbacks always run. If delivery is unavailable, invoke the failure callback outside
    // the lock; callbacks may enqueue another query.
    if (job.OnDone)
        job.OnDone(std::unexpected(std::string("database not running")));
}

void PostgresDatabase::WorkerMain()
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
                    FinishJob(dropped, std::unexpected(std::string("shutdown")), false);
                }
                _queue.clear();
                break;
            }

            job = std::move(_queue.front());
            _queue.pop_front();
        }

        auto* conn = EnsureOpen();
        if (!conn)
        {
            Log::Error("db: '{}' failed - no database connection.", job.Name);
            FinishJob(job, std::unexpected(std::string("no database connection")), false);
            continue;
        }

        if (job.Raw)
        {
            try
            {
                bool ok = job.Raw(*conn);
                FinishJob(job, pqxx::result{}, ok);
            }
            catch (const std::exception& e)
            {
                Log::Error("db: {} failed: {}", job.Name, e.what());
                DropConnection();  // the connection state is unknown; reopen on the next job
                FinishJob(job, std::unexpected(std::string(e.what())), false);
            }
            continue;
        }

        FinishJob(job, RunJob(job, *conn), true);
    }

    DropConnection();
    _prepared.clear();
}

DbResult<pqxx::result> PostgresDatabase::RunJob(Job& job, pqxx::connection& conn)
{
    try
    {
        // Reject a prepared-statement name reused with different SQL.
        if (auto it = _prepared.find(job.Name); it == _prepared.end())
        {
            conn.prepare(job.Name, job.Sql);
            _prepared.emplace(job.Name, job.Sql);
        }
        else if (it->second != job.Sql)
        {
            Log::Error(
                "db: statement name '{}' is already prepared with different SQL - refusing. "
                "Give the two queries distinct names.",
                job.Name);
            return std::unexpected(std::string("prepared statement name collision: " + job.Name));
        }

        pqxx::work txn(conn);
        pqxx::result result = txn.exec(pqxx::prepped{job.Name}, job.Params);
        txn.commit();
        return result;
    }
    catch (const std::exception& e)
    {
        Log::Error("db: {} failed: {}", job.Name, e.what());
        DropConnection();  // reopen (and re-prepare) on the next job
        return std::unexpected(std::string(e.what()));
    }
}

pqxx::connection* PostgresDatabase::EnsureOpen()
{
    if (_connection && _connection->is_open())
        return _connection.get();

    // A reopened socket has no server-side prepared statements; forget the cache so each name
    // is re-prepared on first use against the new connection.
    _prepared.clear();
    try
    {
        _connection = std::make_unique<pqxx::connection>(_connectionString);
        if (_connection->is_open())
        {
            _connected.store(true, std::memory_order_relaxed);
            return _connection.get();
        }
    }
    catch (const std::exception&)
    {
        // Don't log the exception text: a failed pqxx::connection ctor can echo the full DSN
        // (password included). Report a generic, secret-free message instead.
        Log::Error("Database connection failed - check host/port/credentials.");
    }
    DropConnection();
    return nullptr;
}

void PostgresDatabase::DropConnection()
{
    _connection.reset();
    _connected.store(false, std::memory_order_relaxed);
}

void PostgresDatabase::FinishJob(Job& job, DbResult<pqxx::result> result, bool rawOk)
{
    if (job.Wait)
    {
        std::lock_guard lock(job.Wait->M);
        job.Wait->Result = std::move(result);
        job.Wait->RawOk = rawOk;
        job.Wait->Done = true;
        job.Wait->Cv.notify_all();
        return;
    }

    if (job.OnDone)
    {
        std::lock_guard lock(_completionMutex);
        _completions.emplace_back(std::move(job.OnDone), std::move(result));
    }
}

}  // namespace VoltMod
