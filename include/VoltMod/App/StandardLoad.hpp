#pragma once

#include <VoltMod/App/Config/PluginSettings.hpp>
#include <VoltMod/Core/LoadSteps.hpp>
#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Runtime.hpp>
#include <format>
#include <string>
#include <string_view>

namespace VoltMod
{

/** @brief Paths and translation behavior for LoadStandardConfig. */
struct StandardLoadOptions
{
    std::string_view Addon;
    std::string_view SettingsFile = "configs/settings.jsonc";
    /** Whether to load configs/translations. */
    bool Translations = true;
};

/**
 * @brief Run the standard configuration and translation load steps.
 *
 * The required "Configuration" step reads `addons/<Addon>/<SettingsFile>` through
 * TConfig::LoadSettings when available, otherwise JsonConfig::Load. Translation loading then
 * applies `plugin.locale` and reads `addons/<Addon>/configs/translations` when enabled.
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
