#include "Core/ConsoleLogger.hpp"

#include <Color.h>
#include <tier0/dbg.h>

namespace VoltMod::Core
{

void ConsoleLogger::Info(const std::string& message)
{
    ConColorMsg(Color(0, 255, 0, 255), "[%s] ", _prefix);
    Msg("%s\n", message.c_str());
}

void ConsoleLogger::Warn(const std::string& message)
{
    ConColorMsg(Color(255, 255, 0, 255), "[%s] WARN: ", _prefix);
    Msg("%s\n", message.c_str());
}

void ConsoleLogger::Error(const std::string& message)
{
    ConColorMsg(Color(255, 0, 0, 255), "[%s] ERROR: ", _prefix);
    Msg("%s\n", message.c_str());
}

}  // namespace VoltMod::Core
