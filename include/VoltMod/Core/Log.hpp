#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace VoltMod
{

enum class LogLevel : uint8_t
{
    Info,
    Warn,
    Error
};

}  // namespace VoltMod

namespace VoltMod::Log
{

/**
 * @brief Where log lines end up.
 *
 * Invoked on the game thread only. @p message borrows the caller's storage for the duration of
 * the call, so a handler that keeps a line must copy it.
 */
using Handler = std::function<void(LogLevel level, std::string_view message)>;

/**
 * Install the process-wide handler and record the calling thread as the game thread.
 *
 * Set once per load cycle by `Runtime::Initialize`, before anything else logs. A file-static rather
 * than an injected service because logging must work from code that holds no runtime at all -
 * static initializers, engine trampolines, and worker threads.
 */
void SetHandler(Handler handler);

/** True while a handler is installed. The gate the formatting helpers below check. */
bool Enabled();

/**
 * Drop lines below @p level.
 *
 * The host decides what each plugin prints, and the SDK reads that back once a frame. Set here
 * rather than checked per line so the formatting helpers can skip the whole `std::format` call.
 */
void SetMinimumLevel(LogLevel level);

/** The level set by @ref SetMinimumLevel; @ref LogLevel::Info until one is. */
LogLevel MinimumLevel();

/** Whether a line at @p level would be printed at all. */
inline bool Wanted(LogLevel level)
{
    return Enabled() && level >= MinimumLevel();
}

/**
 * Route one formatted line to the handler.
 *
 * The console handler reaches tier0's ConColorMsg/Msg, which is game-thread-only, but the
 * database and HTTP workers log too. A line raised off the installing thread is queued instead
 * and replayed by @ref DeliverPending, so worker diagnostics still reach the console without a
 * worker ever touching the engine.
 */
void Emit(LogLevel level, std::string message);

/** Replay lines queued from worker threads. Game thread only; `Runtime::OnGameFrame` calls it. */
void DeliverPending();

// Formatting is skipped entirely without a handler, and for a level the host does not want:
// diagnostic logging behind a debug gate should cost nothing before SetHandler, after unload, and
// while the plugin is silenced.

template <typename... Args>
void Info(std::format_string<Args...> fmt, Args&&... args)
{
    if (Wanted(LogLevel::Info))
    {
        Emit(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args)
{
    if (Wanted(LogLevel::Warn))
    {
        Emit(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void Error(std::format_string<Args...> fmt, Args&&... args)
{
    if (Wanted(LogLevel::Error))
    {
        Emit(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
    }
}

}  // namespace VoltMod::Log
