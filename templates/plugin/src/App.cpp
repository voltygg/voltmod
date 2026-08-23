#include "App.hpp"

#include <CS2Kit/Api.hpp>

namespace $ns
{

void RegisterCommands(CS2Kit::CommandManager& commands);

bool App::Start()
{
    if (!CS2Kit::LoadStandardConfig(Runtime, Config, {.Addon = "$name"}))
        return false;

    // Permissions and replies stay permissive until the plugin sets Runtime.Policy.
    RegisterCommands(Runtime.Commands);
    return true;
}

}  // namespace $ns
