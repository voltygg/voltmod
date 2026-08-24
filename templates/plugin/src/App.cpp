#include "App.hpp"

#include <VoltMod/Api.hpp>

namespace $ns
{

void RegisterCommands(VoltMod::CommandManager& commands);

bool App::Start()
{
    if (!VoltMod::LoadStandardConfig(Runtime, Config, {.Addon = "$name"}))
        return false;

    // Set Runtime.Policy before registering commands that declare a permission.
    RegisterCommands(Runtime.Commands);
    return true;
}

}  // namespace $ns
