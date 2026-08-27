#include <VoltMod/Core/Log.hpp>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace VoltMod::Log
{

static Sink g_sink;
static std::thread::id g_gameThread{};

static std::mutex g_deferredMutex;
static std::deque<std::pair<LogLevel, std::string>> g_deferred;
/** Read by Drain every game frame, so the common empty case never takes the lock or builds a
 *  container. Only ever set true under @ref g_deferredMutex. */
static std::atomic<bool> g_hasDeferred{false};

/** A worker that logs in a tight failure loop must not grow this without bound; past the cap
 *  the oldest lines go, because the first error is rarely the interesting one. */
static constexpr size_t MaxDeferred = 256;

void SetSink(Sink sink)
{
    g_sink = std::move(sink);
    g_gameThread = std::this_thread::get_id();
}

bool Enabled()
{
    return static_cast<bool>(g_sink);
}

void Emit(LogLevel level, std::string message)
{
    if (!g_sink)
        return;

    if (std::this_thread::get_id() == g_gameThread)
    {
        g_sink(level, message);
        return;
    }

    std::lock_guard lock(g_deferredMutex);
    if (g_deferred.size() >= MaxDeferred)
        g_deferred.pop_front();
    g_deferred.emplace_back(level, std::move(message));
    g_hasDeferred.store(true, std::memory_order_release);
}

void Drain()
{
    if (!g_hasDeferred.load(std::memory_order_acquire))
        return;

    std::deque<std::pair<LogLevel, std::string>> ready;
    {
        std::lock_guard lock(g_deferredMutex);
        ready.swap(g_deferred);
        g_hasDeferred.store(false, std::memory_order_release);
    }

    if (!g_sink)
        return;

    for (const auto& [level, message] : ready)
        g_sink(level, message);
}

}  // namespace VoltMod::Log
