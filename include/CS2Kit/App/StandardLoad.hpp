#pragma once

#include <CS2Kit/Core/LoadReport.hpp>
#include <CS2Kit/Core/Paths.hpp>
#include <CS2Kit/Core/PluginManifest.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <format>
#include <string>
#include <string_view>

namespace CS2Kit::App
{

/** @brief Options for LoadStandardConfig; Addon is the plugin's addon folder name. */
struct StandardLoadOptions
{
    std::string_view Addon;
    std::string_view SettingsFile = "configs/settings.jsonc";
    /** false when the addon ships no configs/translations directory. */
    bool Translations = true;
};

/** Adopt addons/<addon>/<addon>.manifest.json as a Core::LoadReport stage. Absent or malformed
 *  degrades the stage rather than failing the load: the manifest is diagnostics. */
void LoadPluginManifest(std::string_view addon);

/**
 * @brief The standard OnLoad prelude, recorded as LoadReport stages.
 *
 * "Configuration" loads addons/<Addon>/<SettingsFile>, via TConfig::LoadSettings when
 * present (the load-then-validate convention) and JsonConfig::Load otherwise; false
 * means abort the load. "Translations" applies `plugin.locale` when the settings struct
 * carries the standard plugin section, then loads addons/<Addon>/configs/translations.
 * "Manifest" adopts addons/<Addon>/<Addon>.manifest.json, announcing the plugin to peers
 * and queueing its dependency report; a plugin shipping no manifest skips the stage.
 */
template <class TConfig>
bool LoadStandardConfig(TConfig& config, const StandardLoadOptions& options)
{
    auto& report = CS2Kit::Detail::Rt().LoadReport;
    const std::string path = Core::AddonFile(options.Addon, options.SettingsFile);

    const auto status = report.Run("Configuration", [&] {
        const bool loaded = [&] {
            if constexpr (requires { config.LoadSettings(path); })
                return config.LoadSettings(path);
            else
                return config.Load(path);
        }();
        return loaded ? Core::StageResult::Ok(path) : Core::StageResult::Failed(std::format("failed to load {}", path));
    });
    if (status == Core::StageStatus::Failed)
        return false;

    if (options.Translations)
    {
        report.Run("Translations", [&] {
            if constexpr (requires { CS2Kit::Detail::Rt().Translations.SetLanguage(config.Get().plugin.locale); })
                CS2Kit::Detail::Rt().Translations.SetLanguage(config.Get().plugin.locale);
            CS2Kit::Detail::Rt().Translations.Load(Core::AddonFile(options.Addon, "configs/translations"));
            return Core::StageResult::Ok(CS2Kit::Detail::Rt().Translations.GetLanguage());
        });
    }
    LoadPluginManifest(options.Addon);
    return true;
}

}  // namespace CS2Kit::App
