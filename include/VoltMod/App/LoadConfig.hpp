#pragma once

#include <VoltMod/App/Config/PluginSettings.hpp>
#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Core/LoadSteps.hpp>
#include <VoltMod/Runtime.hpp>
#include <format>
#include <string>
#include <string_view>

namespace VoltMod
{

/** @brief Paths and translation behavior for LoadConfig. */
struct LoadConfigOptions
{
    std::string_view SettingsFile = "configs/settings.jsonc";
    /** Whether to load the translations directory. */
    bool Translations = true;
};

/**
 * @brief Load the plugin's settings, then its translations.
 *
 * The required "Configuration" step reads the plugin's @ref LoadConfigOptions::SettingsFile
 * through TConfig::LoadSettings when available, otherwise Options::Load. Translation loading then
 * applies `plugin.locale` and reads the plugin's `translations` directory when enabled.
 */
template <class TConfig>
bool LoadConfig(Runtime& runtime, TConfig& config, const LoadConfigOptions& options = {})
{
    const std::string path = runtime.PluginFile(options.SettingsFile);
    const bool loaded = runtime.LoadSteps.Required("Configuration", [&] {
        Status status = [&] {
            if constexpr (requires { config.LoadSettings(path); })
            {
                return config.LoadSettings(path);
            }
            else
            {
                return config.Load(path);
            }
        }();
        if (!status)
        {
            status.error().Detail = std::format("{}: {}", path, status.error().Detail);
        }
        return status;
    });
    if (!loaded)
    {
        return false;
    }

    if (options.Translations)
    {
        auto& translations = runtime.Translations;
        if constexpr (requires { translations.SetLanguage(config.Get().plugin.locale); })
        {
            translations.SetLanguage(config.Get().plugin.locale);
        }
        translations.Load(runtime.PluginFile("translations"));
    }
    return true;
}

}  // namespace VoltMod
