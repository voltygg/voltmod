#pragma once

#include <VoltMod/App/PluginSettings.hpp>
#include <VoltMod/Core/LoadSteps.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Runtime.hpp>
#include <format>
#include <string>
#include <string_view>

namespace VoltMod
{

/** @brief Options for LoadStandardConfig; Addon is the plugin's addon folder name. */
struct StandardLoadOptions
{
    std::string_view Addon;
    std::string_view SettingsFile = "configs/settings.jsonc";
    /** false when the addon ships no configs/translations directory. */
    bool Translations = true;
};

/**
 * @brief The standard OnLoad prelude.
 *
 * Loads addons/<Addon>/<SettingsFile> as the required "Configuration" step, via
 * TConfig::LoadSettings when present and JsonConfig::Load otherwise. Then applies `plugin.locale`
 * when present and loads addons/<Addon>/configs/translations.
 */
template <class TConfig>
bool LoadStandardConfig(Runtime& runtime, TConfig& config, const StandardLoadOptions& options)
{
    const std::string path = AddonFile(options.Addon, options.SettingsFile);
    const bool loaded = runtime.LoadSteps.Required("Configuration", [&] {
        Status status = [&] {
            if constexpr (requires { config.LoadSettings(path); })
                return config.LoadSettings(path);
            else
                return config.Load(path);
        }();
        if (!status)
            status.error().Detail = std::format("{}: {}", path, status.error().Detail);
        return status;
    });
    if (!loaded)
        return false;

    if (options.Translations)
    {
        auto& translations = runtime.Translations;
        if constexpr (requires { translations.SetLanguage(config.Get().plugin.locale); })
            translations.SetLanguage(config.Get().plugin.locale);
        translations.Load(AddonFile(options.Addon, "configs/translations"));
    }
    return true;
}

}  // namespace VoltMod
