#pragma once

#include <VoltMod/Api.hpp>
#include <VoltMod/App/Config.hpp>

namespace $ns
{

/** Root of settings.jsonc; add a struct + a member here for each new section. Public members are
 *  reflected, so the member name is the JSON key and there is nothing to register. */
struct Settings
{
    VoltMod::StandardPluginSettings plugin;
};

/** Compose VoltMod::JsonConfig in a ConfigManager of your own once you need post-load validation
 *  or derived accessors - see the configuration guide. Do not subclass it: resolving into a value
 *  that has not been published yet is what keeps a failed reload from leaving half-applied state. */
using ConfigManager = VoltMod::JsonConfig<Settings>;

}  // namespace $ns

/** Accepts the `"$schema"` key settings.jsonc names for editor completion. */
