#pragma once

#include <CS2Kit/Core/ILogger.hpp>
#include <format>
#include <string>

namespace CS2Kit::Core::Log
{

template <typename... Args>
void Info(std::format_string<Args...> fmt, Args&&... args)
{
    if (auto* logger = Core::GetGlobalLogger())
        logger->Info(std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args)
{
    if (auto* logger = Core::GetGlobalLogger())
        logger->Warn(std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Error(std::format_string<Args...> fmt, Args&&... args)
{
    if (auto* logger = Core::GetGlobalLogger())
        logger->Error(std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace CS2Kit::Core::Log
