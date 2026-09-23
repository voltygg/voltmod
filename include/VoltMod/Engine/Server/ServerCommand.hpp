#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <functional>
#include <memory>
#include <string_view>

namespace VoltMod
{

/**
 * @brief RAII server console command: registers a tier1 ConCommand on construction and
 * unregisters on destruction.
 *
 * Use this for server-console, RCON, cfg, and loosely coupled automation surfaces. Invoke
 * commands through @ref ConVars::ExecuteServerCommand. Prefer ServiceExchange
 * when two plugins need a typed, versioned contract.
 *
 * The handler runs on the game thread. Construct only while the plugin is loaded (ICvar must be
 * live); typically a manager member, so destruction on unload unregisters it.
 */
class ServerCommand
{
public:
    /** @p slot is the player who typed it in their own console, or -1 for the server. */
    using Handler = std::function<void(const CCommand& args, int slot)>;

    /** Runs for the server console, RCON and cfg files; with @p playersCanRun, also from a
     *  player's own console. Otherwise the engine refuses a player before the handler runs. */
    ServerCommand(std::string_view name, std::string_view helpText, Handler handler, bool playersCanRun = false);
    ~ServerCommand();
    ServerCommand(const ServerCommand&) = delete;
    ServerCommand& operator=(const ServerCommand&) = delete;

private:
    struct Impl;  // tier1's ConCommand, kept out of this header
    std::unique_ptr<Impl> _impl;
};

}  // namespace VoltMod
