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
    using Handler = std::function<void(const CCommand& args)>;
    using PlayerHandler = std::function<void(const CCommand& args, int slot)>;

    /** Runs for the server console, RCON and cfg files only; a player typing it is ignored. */
    ServerCommand(std::string_view name, std::string_view helpText, Handler handler);
    /** Also typeable in a player's own console, where @p slot is theirs; -1 is the server. */
    ServerCommand(std::string_view name, std::string_view helpText, PlayerHandler handler);
    ~ServerCommand();
    ServerCommand(const ServerCommand&) = delete;
    ServerCommand& operator=(const ServerCommand&) = delete;

private:
    struct Impl;  // hides tier1 ConCommand + ICommandCallback so this header stays SDK-free
    std::unique_ptr<Impl> _impl;
};

}  // namespace VoltMod
