#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Database/DbResult.hpp>
#include <VoltMod/Database/Migrator.hpp>
#include <VoltMod/Database/PostgresConfig.hpp>
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
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VoltMod
{

/**
 * @brief Async-first PostgreSQL access layer.
 *
 * One worker thread owns the ONLY connection (opened lazily, reopened on failure); the game
 * thread never blocks on the database during play. Jobs run FIFO, so a write enqueued before a
 * read is visible to it. Completions are queued and replayed on the game thread (a per-frame
 * subscription self-registers in Start), so callbacks may touch engine and plugin state freely.
 *
 * - `Query` / `Exec` are the gameplay path: fire, and (for Query) receive the result later.
 * - The `*Blocking` variants enqueue the same way but wait for the worker - use them ONLY at
 *   load time (OnLoad, migrations, `!admin_reload`); never on a per-frame or per-event path.
 *
 * Shutdown (`Stop`): new work is dropped with a log line, the already-queued jobs get to finish within
 * `stopDeadline` (a ban written just before unload must land), anything past the deadline is
 * dropped with a warning, blocked waiters are released with a failed result, and undispatched
 * completions are destroyed unrun - the state they would touch is going away.
 */
class PostgresDatabase
{
public:
    using ResultCallback = std::move_only_function<void(DbResult<pqxx::result>)>;

    /** @p scheduler drives per-frame completion delivery and must outlive this object. */
    explicit PostgresDatabase(Scheduler& scheduler) : _scheduler(scheduler) {}
    ~PostgresDatabase();
    PostgresDatabase(const PostgresDatabase&) = delete;
    PostgresDatabase& operator=(const PostgresDatabase&) = delete;

    /**
     * Spawn the worker, verify connectivity with a ping, and register per-frame completion
     * delivery with the framework scheduler. Returns false (worker stopped again) when the
     * database is unreachable, so the plugin can degrade instead of queueing into the void.
     */
    bool Start(const PostgresConfig& config);

    /** Let queued jobs finish, then join the worker (see class docs). Idempotent; also runs from
     *  the destructor. */
    void Stop(std::chrono::milliseconds stopDeadline = std::chrono::seconds(5));

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

    Scheduler& _scheduler;
    std::string _connectionString;

    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::deque<Job> _queue;
    bool _accepting = false;
    bool _stopping = false;
    std::chrono::steady_clock::time_point _stopDeadline{};

    std::mutex _completionMutex;
    std::vector<std::pair<ResultCallback, DbResult<pqxx::result>>> _completions;

    std::thread _worker;
    Subscription _onFrame;

    /** Written by the worker, read by the game thread; mirrors _connection's liveness. */
    std::atomic<bool> _connected{false};

    // Worker-thread-only state (no lock needed).
    std::unique_ptr<pqxx::connection> _connection;
    /** Statement name -> the SQL it was prepared with, so a name reused for different SQL is
     *  caught rather than silently running the first one. */
    std::unordered_map<std::string, std::string> _prepared;
};

/**
 * Apply pending forward-only migrations to `db`. Reads `dir` for files named `NNNN_*.sql` (the leading
 * integer is the version), and applies every file whose version exceeds the max recorded in the history
 * table, in ascending order, each in its own transaction, under a session advisory lock so two
 * concurrent plugin loads cannot race. A missing directory is a successful no-op (logged). On failure
 * the database is left at the last successfully applied version.
 */
MigrationResult RunMigrations(PostgresDatabase& db, std::string_view dir, const MigrationOptions& options = {});

}  // namespace VoltMod
