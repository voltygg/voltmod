#pragma once

#include <functional>
#include <memory>

class CCommand;

namespace VoltMod::Engine
{

/**
 * @brief RAII server console command: registers a tier1 ConCommand on construction and
 * unregisters on destruction.
 *
 * Use this for server-console, RCON, cfg, and loosely coupled automation surfaces. Invoke
 * commands through @ref ConVars::ExecuteServerCommand. Prefer App::ServiceExchange
 * when two plugins need a typed, versioned contract.
 *
 * The handler runs on the game thread. Construct only while the plugin is loaded (ICvar
 * must be live); typically a manager member, so destruction on unload unregisters it.
 */
class ServerCommand
{
public:
    using Handler = std::function<void(const CCommand& args)>;

    ServerCommand(const char* name, const char* helpText, Handler handler);
    ~ServerCommand();
    ServerCommand(const ServerCommand&) = delete;
    ServerCommand& operator=(const ServerCommand&) = delete;

private:
    struct Impl;  // hides tier1 ConCommand + ICommandCallback so this header stays SDK-free
    std::unique_ptr<Impl> _impl;
};

}  // namespace VoltMod::Engine
