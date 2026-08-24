#include "App.hpp"

#include <VoltMod/Api.hpp>

namespace $ns
{

void RegisterCommands(VoltMod::CommandManager& commands);

bool App::Start()
{
    if (!VoltMod::LoadStandardConfig(Runtime, Config, {.Addon = "$name"}))
        return false;

    // Permissions and replies stay permissive until the plugin sets Runtime.Policy.
    RegisterCommands(Runtime.Commands);
    return true;
}

}  // namespace $ns
