#pragma once

#include <VoltMod/Api.hpp>
#include <VoltMod/App/Config.hpp>

namespace $namespace
{

/** Root of settings.jsonc; add a struct + a member here for each new section. Public members are
 *  reflected, so the member name is the JSON key and there is nothing to register. */
struct Settings
{
    VoltMod::StandardPluginSettings plugin;
};

/** Name a snapshot type and the function that builds it once you need post-load validation or
 *  derived values - see the configuration guide. Options builds the snapshot before publishing
 *  it, which is what keeps a failed reload from leaving half-applied state. */
using ConfigManager = VoltMod::Options<Settings>;

}  // namespace $namespace
