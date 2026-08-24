#include <CS2Kit/Core/ILogger.hpp>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace CS2Kit::Core
{

namespace
{

ILogger* g_logger = nullptr;
std::thread::id g_gameThread{};

std::mutex g_deferredMutex;
std::vector<std::pair<LogLevel, std::string>> g_deferred;

/** A worker that logs in a tight failure loop must not grow this without bound; past the cap
 *  the oldest lines go, because the first error is rarely the interesting one. */
constexpr size_t MaxDeferred = 256;

void Write(ILogger& logger, LogLevel level, const std::string& message)
{
    switch (level)
    {
    case LogLevel::Info:
        logger.Info(message);
        break;
    case LogLevel::Warn:
        logger.Warn(message);
        break;
    case LogLevel::Error:
        logger.Error(message);
        break;
    }
}

}  // namespace

ILogger* GetGlobalLogger()
{
    return g_logger;
}

void SetGlobalLogger(ILogger* logger)
{
    g_logger = logger;
    g_gameThread = std::this_thread::get_id();
}

void Emit(LogLevel level, std::string message)
{
    if (!g_logger)
        return;

    if (std::this_thread::get_id() == g_gameThread)
    {
        Write(*g_logger, level, message);
        return;
    }

    std::lock_guard lock(g_deferredMutex);
    if (g_deferred.size() >= MaxDeferred)
        g_deferred.erase(g_deferred.begin());
    g_deferred.emplace_back(level, std::move(message));
}

void DrainDeferredLogs()
{
    std::vector<std::pair<LogLevel, std::string>> ready;
    {
        std::lock_guard lock(g_deferredMutex);
        if (g_deferred.empty())
            return;
        ready.swap(g_deferred);
    }

    if (!g_logger)
        return;

    for (const auto& [level, message] : ready)
        Write(*g_logger, level, message);
}

}  // namespace CS2Kit::Core
