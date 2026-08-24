#pragma once

#include <VoltMod/Core/ILogger.hpp>
#include <format>
#include <string>

namespace VoltMod::Core::Log
{

/** No logger means Emit would drop the line, so formatting it is pure waste - and diagnostic
 *  logging behind a debug gate should cost nothing before SetGlobalLogger and after Shutdown. */
inline bool Enabled()
{
    return Core::GetGlobalLogger() != nullptr;
}

template <typename... Args>
void Info(std::format_string<Args...> fmt, Args&&... args)
{
    if (Enabled())
        Core::Emit(Core::LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args)
{
    if (Enabled())
        Core::Emit(Core::LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Error(std::format_string<Args...> fmt, Args&&... args)
{
    if (Enabled())
        Core::Emit(Core::LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace VoltMod::Core::Log
