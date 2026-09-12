#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Database/Connection.hpp>
#include <VoltMod/Database/DatabaseConfig.hpp>
#include <VoltMod/Database/Driver.hpp>
#include <VoltMod/Database/Migrator.hpp>
#include <atomic>
#include <chrono>
#include <concepts>
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
 * @brief Async database access over Postgres, MariaDB and SQLite.
 *
 * One worker thread owns the only connection, opened lazily and reopened on failure. Jobs run
 * FIFO, and completions replay on the game thread, so a callback may touch engine state.
 *
 * A job is a callable over `auto& conn`, compiled for all three connection types and returning
 * the same type from each; dialect differences go in `if constexpr` branches on @ref IsPostgres
 * and friends.
 *
 * @ref Run blocks and is load-time only; anything per-frame or per-event uses @ref RunAsync.
 */
class Database
{
public:
    /** The worker's connection; monostate before the first successful open and after a drop. */
    using AnyConnection = std::variant<std::monostate, PostgresConnection, MariaDbConnection, SqliteConnection>;

    /** What a job returns. @ref CheckJob makes all three drivers agree, so SQLite is an
     *  arbitrary pick. */
    template <class Fn>
    using ResultOf = std::invoke_result_t<Fn&, SqliteConnection&>;

    /** @p scheduler drives per-frame completion delivery and must outlive this object. */
    explicit Database(Scheduler& scheduler) : _scheduler(scheduler) {}
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /** Spawn the worker and ping. False on a bad config or an unreachable database, so a
     *  plugin can degrade rather than queue into the void. */
    bool Start(const DatabaseConfig& config);

    /**
     * Let queued jobs finish within @p stopDeadline, then join the worker, so a ban written just
     * before unload still lands. Past the deadline jobs are dropped and waiters get a failure;
     * undispatched completions are destroyed unrun, since the state they touch is going away.
     * Idempotent, and the destructor calls it.
     */
    void Stop(std::chrono::milliseconds stopDeadline = std::chrono::seconds(5));

    /** Run @p fn on the worker; @p onDone runs on the game thread later. @p name is a log label. */
    template <class Fn>
    void RunAsync(std::string name, Fn fn, std::move_only_function<void(Result<ResultOf<Fn>>)> onDone = {})
    {
        using Value = ResultOf<Fn>;
        CheckJob<Fn>();

        Job job;
        job.Name = std::move(name);
        if (!onDone)
        {
            job.Run = [fn = std::move(fn)](AnyConnection& conn) mutable { Invoke(conn, fn); };
            job.OnFail = [](Error) {};
            Enqueue(std::move(job));
            return;
        }

        // Shared because exactly one of the two paths runs, and a move-only callback cannot be
        // captured by both.
        auto callback = std::make_shared<std::move_only_function<void(Result<Value>)>>(std::move(onDone));
        job.Run = [this, fn = std::move(fn), callback](AnyConnection& conn) mutable {
            if constexpr (std::is_void_v<Value>)
            {
                Invoke(conn, fn);
                PushCompletion([callback] { (*callback)(Result<void>{}); });
            }
            else
            {
                PushCompletion([callback, value = Invoke(conn, fn)]() mutable {
                    (*callback)(Result<Value>{std::move(value)});
                });
            }
        };
        job.OnFail = [this, callback](Error error) {
            PushCompletion(
                [callback, error = std::move(error)]() mutable { (*callback)(std::unexpected(std::move(error))); });
        };
        Enqueue(std::move(job));
    }

    /** @ref RunAsync for a callback wanting the value alone; a failure is dropped, the worker
     *  having logged it. Taking the whole `Result` selects the overload above instead. */
    template <class Fn, class OnValue>
        requires(!std::is_void_v<ResultOf<Fn>> && std::invocable<OnValue&, ResultOf<Fn>> &&
                 !std::invocable<OnValue&, Result<ResultOf<Fn>>>)
    void RunAsync(std::string name, Fn fn, OnValue onValue)
    {
        RunAsync(std::move(name), std::move(fn),
                 std::move_only_function<void(Result<ResultOf<Fn>>)>(
                     [onValue = std::move(onValue)](Result<ResultOf<Fn>> result) mutable {
                         // std::function callbacks are optional at several call sites.
                         if constexpr (requires { static_cast<bool>(onValue); })
                             if (!onValue)
                                 return;
                         if (result)
                             onValue(std::move(*result));
                     }));
    }

    /** Blocking variant of @ref RunAsync - load time only. */
    template <class Fn>
    Result<ResultOf<Fn>> Run(std::string name, Fn fn)
    {
        using Value = ResultOf<Fn>;
        CheckJob<Fn>();

        auto promise = std::make_shared<std::promise<Result<Value>>>();
        std::future<Result<Value>> answer = promise->get_future();

        Job job;
        job.Name = std::move(name);
        job.Run = [fn = std::move(fn), promise](AnyConnection& conn) mutable {
            if constexpr (std::is_void_v<Value>)
            {
                Invoke(conn, fn);
                promise->set_value(Result<void>{});
            }
            else
            {
                promise->set_value(Result<Value>{Invoke(conn, fn)});
            }
        };
        job.OnFail = [promise](Error error) { promise->set_value(std::unexpected(std::move(error))); };
        Enqueue(std::move(job));

        return answer.get();
    }

    /** @ref Run, falling back to @p fallback when the job fails. */
    template <class Fn>
    ResultOf<Fn> RunOr(std::string name, Fn fn, ResultOf<Fn> fallback = {})
    {
        auto result = Run(std::move(name), std::move(fn));
        return result ? std::move(*result) : std::move(fallback);
    }

    /** Invoke all ready completions on the calling (game) thread. Start self-registers this. */
    void DispatchCompletions();

    /** Whether the connection was live as of the worker's last job. It can drop before the
     *  next one, so this is a diagnostic, never a guarantee. */
    bool IsConnected() const { return _connected.load(std::memory_order_relaxed); }

    /** The driver @ref Start parsed from the config. Meaningless before a successful Start. */
    Driver GetDriver() const { return _driver; }

private:
    struct Job
    {
        std::string Name;  ///< log label only
        std::move_only_function<void(AnyConnection&)> Run;
        std::move_only_function<void(Error)> OnFail;
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
