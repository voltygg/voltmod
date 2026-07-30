#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace CS2Kit::Core
{

/** @brief The kit-standard "plugin" section of settings.jsonc. Embed it as
 *  `CS2Kit::StandardPluginSettings plugin;` in the root settings struct;
 *  LoadStandardConfig applies the locale to Engine().Translations. */
struct StandardPluginSettings
{
    std::string locale = "en";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StandardPluginSettings, locale)

}  // namespace CS2Kit::Core
