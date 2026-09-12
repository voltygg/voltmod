#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Database/Connection.hpp>
#include <VoltMod/Database/DatabaseConfig.hpp>
#include <VoltMod/Database/DbResult.hpp>
#include <VoltMod/Database/Driver.hpp>
#include <VoltMod/Database/Migrator.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace VoltMod
{

/**
 * @brief Async-first database access layer over Postgres, MariaDB and SQLite.
 *
 * One worker thread owns the ONLY connection (opened lazily, reopened on failure); the game
 * thread never blocks on the database during play. Jobs run FIFO, so a write enqueued before a
 * read is visible to it. Completions are queued and replayed on the game thread (a per-frame
 * subscription self-registers in Start), so callbacks may touch engine and plugin state freely.
 *
 * A job is a generic callable taking `auto& conn`: it is compiled for all three connection types
 * and must return the same type from each, so dialect differences go in `if constexpr` branches
 * on @ref IsPostgres and friends.
 *
 * - `Run` is the gameplay path: fire, and receive the result later on the game thread.
 * - `RunBlocking` enqueues the same way but waits for the worker - use it ONLY at load time
 *   (OnLoad, migrations, `!admin_reload`); never on a per-frame or per-event path.
 *
 * Shutdown (`Stop`): new work is dropped with a log line, the already-queued jobs get to finish
 * within `stopDeadline` (a ban written just before unload must land), anything past the deadline
 * is dropped with a warning, blocked waiters are released with a failed result, and undispatched
 * completions are destroyed unrun - the state they would touch is going away.
 */
class Database
{
public:
    /** The worker's connection; monostate before the first successful open and after a drop. */
    using AnyConnection = std::variant<std::monostate, PostgresConnection, MariaDbConnection, SqliteConnection>;

    /** What a job returns. Instantiated on one connection type; all three must agree. */
    template <class Fn>
    using ResultOf = std::invoke_result_t<Fn&, SqliteConnection&>;

    /** @p scheduler drives per-frame completion delivery and must outlive this object. */
    explicit Database(Scheduler& scheduler) : _scheduler(scheduler) {}
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /**
     * Parse the driver, spawn the worker, verify connectivity with a ping, and register
     * per-frame completion delivery with the framework scheduler. Returns false (worker stopped
     * again) on an invalid config or an unreachable database, so the plugin can degrade instead
     * of queueing into the void.
     */
    bool Start(const DatabaseConfig& config);

    /** Let queued jobs finish, then join the worker (see class docs). Idempotent; also runs from
     *  the destructor. */
    void Stop(std::chrono::milliseconds stopDeadline = std::chrono::seconds(5));

    /** Run @p fn on the worker; @p onDone runs on the game thread later. @p name is a log label. */
    template <class Fn>
    void Run(std::string name, Fn fn, std::move_only_function<void(DbResult<ResultOf<Fn>>)> onDone = {})
    {
        using Result = ResultOf<Fn>;
        CheckJob<Fn>();

        Job job;
        job.Name = std::move(name);
        if (!onDone)
        {
            job.Body = [fn = std::move(fn)](AnyConnection& conn) mutable { Invoke(conn, fn); };
            job.Fail = [](std::string) {};
            Enqueue(std::move(job));
            return;
        }

        // Shared because exactly one of the two paths runs, and a move-only callback cannot be
        // captured by both.
        auto callback = std::make_shared<std::move_only_function<void(DbResult<Result>)>>(std::move(onDone));
        job.Body = [this, fn = std::move(fn), callback](AnyConnection& conn) mutable {
            if constexpr (std::is_void_v<Result>)
            {
                Invoke(conn, fn);
                PushCompletion([callback] { (*callback)(DbResult<void>{}); });
            }
            else
            {
                PushCompletion([callback, value = Invoke(conn, fn)]() mutable {
                    (*callback)(DbResult<Result>{std::move(value)});
                });
            }
        };
        job.Fail = [this, callback](std::string message) {
            PushCompletion([callback, message = std::move(message)]() mutable {
                (*callback)(std::unexpected(std::move(message)));
            });
        };
        Enqueue(std::move(job));
    }

    /** Blocking variant of @ref Run - load time only. */
    template <class Fn>
    DbResult<ResultOf<Fn>> RunBlocking(std::string name, Fn fn)
    {
        using Result = ResultOf<Fn>;
        CheckJob<Fn>();

        auto promise = std::make_shared<std::promise<DbResult<Result>>>();
        std::future<DbResult<Result>> answer = promise->get_future();

        Job job;
        job.Name = std::move(name);
        job.Body = [fn = std::move(fn), promise](AnyConnection& conn) mutable {
            if constexpr (std::is_void_v<Result>)
            {
                Invoke(conn, fn);
                promise->set_value(DbResult<void>{});
            }
            else
            {
                promise->set_value(DbResult<Result>{Invoke(conn, fn)});
            }
        };
        job.Fail = [promise](std::string message) { promise->set_value(std::unexpected(std::move(message))); };
        Enqueue(std::move(job));

        return answer.get();
    }

    /** Invoke all ready completions on the calling (game) thread. Start self-registers this. */
    void DispatchCompletions();

    /**
     * Whether the worker's connection was live as of its last job - safe to read from the game
     * thread, and the only runtime health signal there is. It is a report, not a reservation: the
     * connection can drop before the next job, so use it for diagnostics and fast-fail, never as
     * a guarantee that an about-to-be-enqueued write will land.
     */
    bool IsConnected() const { return _connected.load(std::memory_order_relaxed); }

    /** The driver @ref Start parsed from the config. Meaningless before a successful Start. */
    Driver GetDriver() const { return _driver; }

private:
    struct Job
    {
        std::string Name;  ///< log label only
        std::move_only_function<void(AnyConnection&)> Body;
        std::move_only_function<void(std::string)> Fail;
    };

    /** A job must compile and return the same type on every driver. */
    template <class Fn>
    static void CheckJob()
    {
        static_assert(std::same_as<std::invoke_result_t<Fn&, PostgresConnection&>, ResultOf<Fn>> &&
                          std::same_as<std::invoke_result_t<Fn&, MariaDbConnection&>, ResultOf<Fn>>,
                      "a database job must return the same type for every driver");
    }

    template <class Fn>
    static ResultOf<Fn> Invoke(AnyConnection& conn, Fn& fn)
    {
        return std::visit(
            [&fn](auto& open) -> ResultOf<Fn> {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(open)>, std::monostate>)
                    throw std::logic_error("no database connection");
                else
                    return fn(open);
            },
            conn);
    }

    void Enqueue(Job job);
    void PushCompletion(std::move_only_function<void()> completion);
    void WorkerMain();
    /** Open/reopen the worker's connection; false on failure (secret-free log). */
    bool EnsureOpen();
    /** Drop the connection so the next job reopens it, keeping @ref IsConnected in step. */
    void DropConnection();

    Scheduler& _scheduler;
    DatabaseConfig _config;
    Driver _driver = Driver::Postgres;

    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::deque<Job> _queue;
    bool _accepting = false;
    bool _stopping = false;
    std::chrono::steady_clock::time_point _stopDeadline{};

    std::mutex _completionMutex;
    std::vector<std::move_only_function<void()>> _completions;

    std::thread _worker;
    Subscription _onFrame;

    /** Written by the worker, read by the game thread; mirrors _connection's liveness. */
    std::atomic<bool> _connected{false};

    /** Worker-thread-only state (no lock needed). */
    AnyConnection _connection;
};

/**
 * Apply pending forward-only migrations to `db` from `dir`/<driver name>. Reads files named
 * `NNNN_*.sql` (the leading integer is the version), and applies every file whose version exceeds
 * the max recorded in the history table, in ascending order, each in its own transaction, under a
 * lock so two concurrent plugin loads cannot race. A missing directory is a successful no-op
 * (logged). On failure the database is left at the last successfully applied version.
 */
MigrationResult RunMigrations(Database& db, std::string_view dir, const MigrationOptions& options = {});

}  // namespace VoltMod
