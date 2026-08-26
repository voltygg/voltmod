#include "App.hpp"

#include <VoltMod/Api.hpp>

namespace $ns
{

void RegisterCommands(VoltMod::CommandManager& commands, std::vector<VoltMod::Subscription>& subs);

bool App::Start()
{
    if (!VoltMod::LoadStandardConfig(Runtime, Config, {.Addon = "$name"}))
        return false;

    // Fill in Runtime.Policy (HasPermission at least) before registering commands that
    // declare a permission: Policy::Authorize denies them while it is unset.
    RegisterCommands(Runtime.Commands, _subs);
    return true;
}

}  // namespace $ns
