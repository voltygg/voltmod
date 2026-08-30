#include "App.hpp"

#include <VoltMod/Api.hpp>

namespace SchemaDump
{

void RegisterCommands(App& app);

bool App::Start()
{
    if (!VoltMod::LoadStandardConfig(Runtime, Config, {.Addon = "schema-dump"}))
        return false;

    // Fill in Runtime.Policy (HasPermission at least) before registering commands that
    // declare a permission: Policy::Authorize denies them while it is unset.
    RegisterCommands(*this);
    return true;
}

}  // namespace SchemaDump
