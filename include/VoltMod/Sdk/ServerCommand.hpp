#pragma once

#include <functional>
#include <memory>

class CCommand;

namespace VoltMod::Sdk
{

/**
 * @brief RAII server console command: registers a tier1 ConCommand on construction and
 * unregisters on destruction.
 *
 * This is the standard cross-plugin surface: plugins are isolated modules, so a feature
 * that another plugin (or the server console / a cfg) should drive is exposed as a server
 * command and invoked via @ref ConVarService::ExecuteServerCommand. When the providing
 * plugin is absent the engine just logs "Unknown command" - graceful degradation for free.
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

}  // namespace VoltMod::Sdk
