#pragma once

#include "Host/Loading/PluginLoader.hpp"
#include "Host/Plugins/PluginHost.hpp"

#include <VoltMod/Engine/Server/ServerCommand.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The `volt` console command: what the server operator reads and asks for.
 *
 * Registered before any plugin loads, so a plugin registering `volt` is refused with the usual
 * conflict line rather than fighting the host for the tier1 command.
 */
class VoltCommand
{
public:
    VoltCommand(PluginHost& host, PluginLoader& loader);
    ~VoltCommand();

    VoltCommand(const VoltCommand&) = delete;
    VoltCommand& operator=(const VoltCommand&) = delete;

private:
    void Run(const CCommand& arguments);
    void PrintLoaded() const;
    void PrintStatus(std::string_view name);
    /** `volt log <name> <level>`: silence one plugin below @p level. */
    void SetLogLevel(std::string_view name, std::string_view level);

    PluginHost& _host;
    PluginLoader& _loader;
    ServerCommand _command;
};

}  // namespace VoltMod
