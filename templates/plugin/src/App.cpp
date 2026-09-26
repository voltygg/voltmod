#include "App.hpp"

#include <VoltMod/Api.hpp>

namespace $namespace
{

void RegisterCommands(VoltMod::CommandManager& commands);

bool App::Load()
{
    RegisterCommands(Runtime.Commands);
    return true;
}

}  // namespace $namespace
