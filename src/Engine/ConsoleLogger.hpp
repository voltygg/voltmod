#pragma once

#include <VoltMod/Core/ILogger.hpp>
#include <string>

namespace VoltMod
{

/**
 * @brief Default `ConColorMsg` logger used when LoadContext has no custom logger.
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

}  // namespace VoltMod
