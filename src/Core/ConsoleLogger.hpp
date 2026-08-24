#pragma once

#include <VoltMod/Core/ILogger.hpp>
#include <string>

namespace VoltMod::Core
{

/**
 * @brief Default console logger implementation using HL2SDK's ConColorMsg.
 * Created automatically by VoltMod::Initialize() when no custom logger is provided.
 */
class ConsoleLogger : public ILogger
{
public:
    void SetPrefix(const char* prefix) { _prefix = prefix; }

    void Info(const std::string& message) override;
    void Warn(const std::string& message) override;
    void Error(const std::string& message) override;

private:
    const char* _prefix = "VoltMod";
};

}  // namespace VoltMod::Core
