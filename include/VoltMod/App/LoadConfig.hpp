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
 * @brief Load the plugin's settings, then its translations, and return @p config.
 *
 * Meant for the App's `Config` member initializer, so every member below it is built with settings:
 * `ConfigManager Config = VoltMod::LoadConfig<ConfigManager>(Runtime);`.
 *
 * The required "Configuration" step reads the plugin's @ref LoadConfigOptions::SettingsFile
 * through TConfig::LoadSettings when available, otherwise Options::Load. A failure keeps the
 * defaults, and the framework refuses the plugin before `Load` runs. Translation loading then
 * applies `plugin.locale` and reads the plugin's `translations` directory when enabled.
 */
template <class TConfig>
TConfig LoadConfig(Runtime& runtime, TConfig config = {}, const LoadConfigOptions& options = {})
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
        // A parse error already names the file.
        if (!status && !status.error().Detail.starts_with(path))
        {
            status.error().Detail = std::format("{}: {}", path, status.error().Detail);
        }
        return status;
    });
    if (!loaded)
    {
        return config;
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
    return config;
}

}  // namespace VoltMod
