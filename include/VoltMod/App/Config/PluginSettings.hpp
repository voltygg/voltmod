#pragma once

#include <string>

namespace VoltMod
{

/** @brief The framework-standard "plugin" section of settings.jsonc; embed it in the root
 *  settings struct. LoadStandardConfig applies the locale to runtime.Translations. */
struct StandardPluginSettings
{
    std::string locale = "en";
};

}  // namespace VoltMod
