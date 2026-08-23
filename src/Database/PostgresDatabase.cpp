#include <CS2Kit/Core/Log.hpp>
#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Database/PostgresDatabase.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <format>

namespace CS2Kit::Database
{

namespace Log = CS2Kit::Core::Log;

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

    if (auto* core = CS2Kit::Detail::RtOrNull())
        _completionPump = core->Scheduler.EveryFrame([this] { DispatchCompletions(); });
    return true;
}

void PostgresDatabase::Stop(std::chrono::milliseconds drainDeadline)
{
    {
        std::lock_guard lock(_queueMutex);
        if (!_worker.joinable() && !_accepting)
            return;
        _accepting = false;
        _stopping = true;
        _drainDeadline = std::chrono::steady_clock::now() + drainDeadline;
    }
    _queueCv.notify_all();

    if (_worker.joinable())
        _worker.join();

    _completionPump.Reset();

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

    // Fail the completion rather than dropping it. Callers treat Query as "the callback always
    // runs", so a silent drop left their state machines waiting forever - and with no connection
    // at all (bad credentials, say) that was every query for the whole session. Enqueue is
    // game-thread-only, so this runs the callback directly: the completion pump may not exist yet.
    // Run it outside the lock, since a callback is free to enqueue again.
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

            // Draining past the deadline: drop what's left (a dead database must not hang unload).
            if (_stopping && std::chrono::steady_clock::now() >= _drainDeadline)
            {
                for (auto& dropped : _queue)
                {
                    Log::Warn("db: dropping queued '{}' - shutdown drain deadline reached.", dropped.Name);
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
        // Key on the SQL too: the cache used to trust the name alone, so a second job reusing a
        // name with different SQL silently executed the first job's statement.
        if (auto it = _prepared.find(job.Name); it == _prepared.end())
        {
            conn.prepare(job.Name, job.Sql);
            _prepared.emplace(job.Name, job.Sql);
        }
        else if (it->second != job.Sql)
        {
            Log::Error("db: statement name '{}' is already prepared with different SQL - refusing. "
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

}  // namespace CS2Kit::Database
