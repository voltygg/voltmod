#pragma once

#include <VoltMod/Core/Log.hpp>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

namespace VoltMod
{

/**
 * @brief Logs through @ref VoltMod::Log with the name of @p T in front of every line.
 *
 * Give the class that logs a member - `Logger<BhopManager> _log;` - and `_log.Info("ready")`
 * reaches the handler as `[BhopManager] ready`. The host adds the plugin's own tag, so a line
 * reads `[BHOP] [BhopManager] ready`: which plugin, then which part of it.
 *
 * Empty and copyable; it holds nothing. Levels are not its business - the host sets each
 * plugin's minimum level and `Log::` drops the rest.
 */
template <class T>
class Logger
{
public:
    template <class... Args>
    void Info(std::format_string<Args...> fmt, Args&&... args) const
    {
        // Check the gate before formatting, as Log:: does: a dropped line must cost nothing.
        if (Log::Wanted(LogLevel::Info))
            Log::Info("[{}] {}", TypeName(), std::format(fmt, std::forward<Args>(args)...));
    }

    template <class... Args>
    void Warn(std::format_string<Args...> fmt, Args&&... args) const
    {
        if (Log::Wanted(LogLevel::Warn))
            Log::Warn("[{}] {}", TypeName(), std::format(fmt, std::forward<Args>(args)...));
    }

    template <class... Args>
    void Error(std::format_string<Args...> fmt, Args&&... args) const
    {
        if (Log::Wanted(LogLevel::Error))
            Log::Error("[{}] {}", TypeName(), std::format(fmt, std::forward<Args>(args)...));
    }

private:
    /** @p T spelled without its namespaces or template arguments, cut out of this signature. */
    static constexpr std::string_view TypeName()
    {
#if defined(_MSC_VER) && !defined(__clang__)
        // "...__cdecl VoltMod::Logger<class VoltMod::BhopManager>::TypeName(void)"
        constexpr std::string_view signature = __FUNCSIG__;
        constexpr std::size_t start = signature.find("Logger<") + 7;
        constexpr std::size_t end = signature.rfind(">::");
#else
        // GCC "[with T = VoltMod::BhopManager; ...]", Clang "[T = VoltMod::BhopManager]".
        constexpr std::string_view signature = __PRETTY_FUNCTION__;
        constexpr std::size_t start = signature.find("T = ") + 4;
        constexpr std::size_t semicolon = signature.find(';', start);
        constexpr std::size_t bracket = signature.find(']', start);
        constexpr std::size_t end = semicolon < bracket ? semicolon : bracket;
#endif
        std::string_view name = signature.substr(start, end - start);

        // Leaves "Row" for "class VoltMod::Menu::Row" and for "Table<int>".
        name = name.substr(0, name.find('<'));
        if (const std::size_t lastColon = name.rfind(':'); lastColon != std::string_view::npos)
            name.remove_prefix(lastColon + 1);
        if (const std::size_t lastSpace = name.rfind(' '); lastSpace != std::string_view::npos)
            name.remove_prefix(lastSpace + 1);

        return name;
    }
};

}  // namespace VoltMod
