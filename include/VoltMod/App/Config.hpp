#pragma once

// Everything a plugin's own Config.hpp needs to declare a settings.jsonc-backed struct:
// VoltMod::Json's (de)serialization helpers, VoltMod::JsonConfig<T>, and the standard
// "plugin" section LoadStandardConfig reads the locale from. Kept out of <VoltMod/Api.hpp>
// so an ordinary translation unit does not pull in nlohmann just by including the umbrella.

#include <VoltMod/App/JsonConfig.hpp>
#include <VoltMod/App/PluginSettings.hpp>
#include <VoltMod/Core/Json.hpp>
