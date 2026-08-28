#include "Engine/ConsoleLogger.hpp"

#include <Color.h>
#include <string_view>
#include <tier0/dbg.h>
#include <utility>

namespace VoltMod
{

Log::Handler MakeConsoleHandler(std::string prefix)
{
    return [prefix = std::move(prefix)](LogLevel level, std::string_view message) {
        switch (level)
        {
        case LogLevel::Info:
            ConColorMsg(Color(0, 255, 0, 255), "[%s] ", prefix.c_str());
            break;
        case LogLevel::Warn:
            ConColorMsg(Color(255, 255, 0, 255), "[%s] WARN: ", prefix.c_str());
            break;
        case LogLevel::Error:
            ConColorMsg(Color(255, 0, 0, 255), "[%s] ERROR: ", prefix.c_str());
            break;
        }
        // string_view is not guaranteed null-terminated, so bound the format explicitly.
        Msg("%.*s\n", static_cast<int>(message.size()), message.data());
    };
}

}  // namespace VoltMod
