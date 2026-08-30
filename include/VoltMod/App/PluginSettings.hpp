#pragma once

// Deliberately no JSON include: this header is reachable from <VoltMod/Api.hpp> through
// StandardLoad.hpp, and RootApiSurfaceTest fails the build if the umbrella reaches Glaze.
// Reflection needs no registration, so the struct is just a struct.
#include <concepts>
#include <string>
#include <string_view>

namespace VoltMod
{

/** @brief The framework-standard "plugin" section of settings.jsonc; embed it in the root
 *  settings struct. LoadStandardConfig applies the locale to runtime.Translations. */
struct StandardPluginSettings
{
    std::string locale = "en";
};

/** @brief A settings root carrying the framework-standard `plugin` section.
 *
 *  LoadStandardConfig applies `plugin.locale` only to a config that satisfies this; a root that
 *  does not is told so in the load report rather than silently keeping the default. */
template <class TSettings>
concept HasPluginSection = requires(const TSettings& settings) {
    { settings.plugin } -> std::convertible_to<const StandardPluginSettings&>;
};

/** @brief A config that validates as part of loading (the load-then-validate convention).
 *
 *  LoadStandardConfig prefers `LoadSettings` over `Load` for these. */
template <class TConfig>
concept HasLoadSettings = requires(TConfig& config, std::string_view path) { config.LoadSettings(path); };

}  // namespace VoltMod
