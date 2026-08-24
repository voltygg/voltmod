#pragma once

#include <VoltMod/Api.hpp>
#include <nlohmann/json.hpp>

namespace $ns
{

/** Root of settings.jsonc; add a struct + a member here for each new section. */
struct Settings
{
    VoltMod::StandardPluginSettings plugin;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, plugin)

/** Subclass VoltMod::JsonConfig instead once you need post-load validation or accessors. */
using ConfigManager = VoltMod::JsonConfig<Settings>;

}  // namespace $ns
