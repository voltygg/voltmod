#include "App.hpp"

#include <VoltMod/Api.hpp>
#include <VoltMod/App/PluginEntry.hpp>

VOLTMOD_PLUGIN($namespace::App);

namespace $namespace
{

void RegisterCommands(VoltMod::CommandManager& commands);

bool App::Load()
{
    if (!VoltMod::LoadStandardConfig(Runtime, Config))
        return false;

    // Set Runtime.Policy before registering permission-gated commands.
    RegisterCommands(Runtime.Commands);
    return true;
}

}  // namespace $namespace
