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
 * Set once per load cycle by `Runtime::Start`, before anything else logs. A file-static rather
 * than an injected service because logging must work from code that holds no runtime at all -
 * static initializers, engine trampolines, and worker threads.
 */
void SetHandler(Handler handler);

/** True while a handler is installed. The gate the formatting helpers below check. */
bool Enabled();

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

// Formatting is skipped entirely without a handler: diagnostic logging behind a debug gate should
// cost nothing before SetHandler and after unload.

template <typename... Args>
void Info(std::format_string<Args...> fmt, Args&&... args)
{
    if (Enabled())
        Emit(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args)
{
    if (Enabled())
        Emit(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Error(std::format_string<Args...> fmt, Args&&... args)
{
    if (Enabled())
        Emit(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace VoltMod::Log
