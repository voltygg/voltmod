#pragma once

#include <CS2Kit/Api.hpp>
#include <nlohmann/json.hpp>

namespace $ns
{

/** Root of settings.jsonc; add a struct + a member here for each new section. */
struct Settings
{
    CS2Kit::StandardPluginSettings plugin;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, plugin)

/** Subclass CS2Kit::JsonConfig instead once you need post-load validation or accessors. */
using ConfigManager = CS2Kit::JsonConfig<Settings>;

}  // namespace $ns
