#pragma once

#include <cstdint>
#include <string>

namespace VoltMod
{

/**
 * @brief interface that consumers implement to provide their own logging backend.
 *
 * The library uses this interface for all log output instead of directly calling
 * HL2SDK's ConColorMsg. Consumers implement this to log to console, file, or
 * any other destination.
 */
class ILogger
{
public:
    virtual ~ILogger() = default;

    virtual void Info(const std::string& message) = 0;
    virtual void Warn(const std::string& message) = 0;
    virtual void Error(const std::string& message) = 0;
};

/** Global logger accessor. Consumers must call SetGlobalLogger() during initialization. */
ILogger* GetGlobalLogger();

/** Install @p logger and record the calling thread as the one it may be invoked on. */
void SetGlobalLogger(ILogger* logger);

enum class LogLevel : uint8_t
{
    Info,
    Warn,
    Error
};

/**
 * Route one formatted line to the global logger.
 *
 * The default ConsoleLogger reaches tier0's ConColorMsg/Msg, which is game-thread-only, but
 * the database and HTTP workers log too. A line raised off the installing thread is queued
 * instead and replayed by @ref DrainDeferredLogs, so worker diagnostics still reach the console
 * without a worker ever touching the engine.
 */
void Emit(LogLevel level, std::string message);

/** Replay lines queued from worker threads. Game thread only; called once per frame. */
void DrainDeferredLogs();

}  // namespace VoltMod
