#pragma once

#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Database/DbResult.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VoltMod::Database
{

/** PostgreSQL connection parameters. Field names are lowercase so a consumer's JSON config
 *  section can map onto them directly (e.g. via nlohmann's non-intrusive macros). */
struct PostgresConfig
{
    std::string host = "localhost";
    int port = 5432;
    std::string database = "voltmod_server";
    std::string username = "voltmod_plugin";
    std::string password;
    std::string sslMode = "prefer";
    /** Bounds every (re)connect attempt so a dead database can't hang queries or unload. */
    int connectTimeoutSec = 5;

    std::string GetConnectionString() const;
};

/**
 * @brief Async-first PostgreSQL access layer.
 *
 * One worker thread owns the ONLY connection (opened lazily, reopened on failure); the game
 * thread never blocks on the database during play. Jobs run FIFO, so a write enqueued before a
 * read is visible to it. Completions are queued and replayed on the game thread (a per-frame
 * pump self-registers in Start), so callbacks may touch engine and plugin state freely.
 *
 * - `Query` / `Exec` are the gameplay path: fire, and (for Query) receive the result later.
 * - The `*Blocking` variants enqueue the same way but wait for the worker - use them ONLY at
 *   load time (OnLoad, migrations, `!admin_reload`); never on a per-frame or per-event path.
 *
 * Shutdown (`Stop`): new work is dropped with a log line, the already-queued jobs drain within
 * `drainDeadline` (a ban written just before unload must land), anything past the deadline is
 * dropped with a warning, blocked waiters are released with a failed result, and undispatched
 * completions are destroyed unrun - the state they would touch is going away.
 */
class PostgresDatabase
{
public:
    using ResultCallback = std::move_only_function<void(DbResult<pqxx::result>)>;

    PostgresDatabase() = default;
    ~PostgresDatabase();
    PostgresDatabase(const PostgresDatabase&) = delete;
    PostgresDatabase& operator=(const PostgresDatabase&) = delete;

    /**
     * Spawn the worker, verify connectivity with a ping, and register the completion pump with
     * the framework scheduler. Returns false (worker stopped again) when the database is unreachable,
     * so the plugin can degrade instead of queueing into the void.
     */
    bool Start(const PostgresConfig& config);

    /** Drain + join the worker (see class docs). Idempotent; also runs from the destructor. */
    void Stop(std::chrono::milliseconds drainDeadline = std::chrono::seconds(5));

    /** Run a named prepared statement off-thread; @p onDone runs on the game thread later. */
    void Query(std::string name, std::string sql, pqxx::params params, ResultCallback onDone);

    /** Fire-and-forget write; failures are logged with @p name. */
    void Exec(std::string name, std::string sql, pqxx::params params = {});

    /** Blocking variant of Query - load time only. */
    DbResult<pqxx::result> QueryBlocking(const std::string& name, const std::string& sql, pqxx::params params = {});

    /** Run @p fn against the live connection on the worker, blocking until done - load time
     *  only. For multi-statement work that manages its own transactions (the migration runner). */
    DbResult<bool> WithConnection(std::function<bool(pqxx::connection&)> fn);

    /** Invoke all ready completions on the calling (game) thread. Start self-registers this. */
    void DispatchCompletions();

    /**
     * Whether the worker's connection was live as of its last job - safe to read from the game
     * thread, and the only runtime health signal there is. It is a report, not a reservation: the
     * connection can drop before the next job, so use it for diagnostics and fast-fail, never as
     * a guarantee that an about-to-be-enqueued write will land.
     */
    bool IsConnected() const { return _connected.load(std::memory_order_relaxed); }

private:
    struct Waiter
    {
        std::mutex M;
        std::condition_variable Cv;
        bool Done = false;
        DbResult<pqxx::result> Result = std::unexpected(std::string("pending"));
        bool RawOk = false;
    };

    struct Job
    {
        std::string Name;  ///< prepared-statement name; also the log label
        std::string Sql;
        pqxx::params Params;
        std::function<bool(pqxx::connection&)> Raw;  ///< WithConnection body (Sql empty)
        ResultCallback OnDone;                       ///< async completion (may be null)
        std::shared_ptr<Waiter> Wait;                ///< blocking rendezvous (may be null)
    };

    void Enqueue(Job job);
    void WorkerMain();
    DbResult<pqxx::result> RunJob(Job& job, pqxx::connection& conn);
    /** Open/reopen the worker's connection; returns null on failure (secret-free log). */
    pqxx::connection* EnsureOpen();
    /** Drop the connection so the next job reopens it, keeping @ref IsConnected in step. */
    void DropConnection();
    void FinishJob(Job& job, DbResult<pqxx::result> result, bool rawOk);

    std::string _connectionString;

    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::deque<Job> _queue;
    bool _accepting = false;
    bool _stopping = false;
    std::chrono::steady_clock::time_point _drainDeadline{};

    std::mutex _completionMutex;
    std::vector<std::pair<ResultCallback, DbResult<pqxx::result>>> _completions;

    std::thread _worker;
    Core::Subscription _completionPump;

    /** Written by the worker, read by the game thread; mirrors _connection's liveness. */
    std::atomic<bool> _connected{false};

    // Worker-thread-only state (no lock needed).
    std::unique_ptr<pqxx::connection> _connection;
    /** Statement name -> the SQL it was prepared with, so a name reused for different SQL is
     *  caught rather than silently running the first one. */
    std::unordered_map<std::string, std::string> _prepared;
};

}  // namespace VoltMod::Database
