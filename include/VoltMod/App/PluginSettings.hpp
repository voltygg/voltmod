#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace VoltMod
{

/** @brief The framework-standard "plugin" section of settings.jsonc; embed it in the root
 *  settings struct. LoadStandardConfig applies the locale to runtime.Translations. */
struct StandardPluginSettings
{
    std::string locale = "en";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StandardPluginSettings, locale)

}  // namespace VoltMod
