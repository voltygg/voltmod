#pragma once

#include <VoltMod/Api.hpp>
#include <VoltMod/App/Config.hpp>

namespace SchemaDump
{

/** Root of settings.jsonc. Public members are reflected, so the member name is the JSON key and
 *  there is nothing to register. This plugin is a dev tool and has no settings of its own. */
struct Settings
{
    VoltMod::StandardPluginSettings plugin;
};

using ConfigManager = VoltMod::JsonConfig<Settings>;

}  // namespace SchemaDump
