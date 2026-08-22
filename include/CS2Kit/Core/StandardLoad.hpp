#pragma once

#include <CS2Kit/Core/LoadReport.hpp>
#include <CS2Kit/Core/Paths.hpp>
#include <CS2Kit/Core/PluginManifest.hpp>
#include <CS2Kit/Core/Services.hpp>
#include <format>
#include <string>
#include <string_view>

namespace CS2Kit::Core
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
 * @brief The standard OnLoad prelude, recorded as LoadReport stages.
 *
 * "Configuration" loads addons/<Addon>/<SettingsFile>, via TConfig::LoadSettings when
 * present (the load-then-validate convention) and JsonConfig::Load otherwise; false
 * means abort the load. "Translations" applies `plugin.locale` when the settings struct
 * carries the standard plugin section, then loads addons/<Addon>/configs/translations.
 * "Manifest" adopts addons/<Addon>/<Addon>.manifest.json when cs2_add_plugin generated
 * one, which announces this plugin to peers and queues its dependency report; a plugin
 * that ships no manifest simply skips the stage.
 */
/** Read and adopt addons/<addon>/<addon>.manifest.json, recording a LoadReport stage.
 *  Absent or malformed is a degraded stage, never a failed load - the manifest is
 *  diagnostics. */
void LoadPluginManifest(std::string_view addon);

template <class TConfig>
bool LoadStandardConfig(TConfig& config, const StandardLoadOptions& options)
{
    auto& report = Engine().LoadReport;
    const std::string path = AddonFile(options.Addon, options.SettingsFile);

    const auto status = report.Run("Configuration", [&] {
        const bool loaded = [&] {
            if constexpr (requires { config.LoadSettings(path); })
                return config.LoadSettings(path);
            else
                return config.Load(path);
        }();
        return loaded ? StageResult::Ok(path) : StageResult::Failed(std::format("failed to load {}", path));
    });
    if (status == StageStatus::Failed)
        return false;

    if (options.Translations)
    {
        report.Run("Translations", [&] {
            if constexpr (requires { Engine().Translations.SetLanguage(config.Get().plugin.locale); })
                Engine().Translations.SetLanguage(config.Get().plugin.locale);
            Engine().Translations.Load(AddonFile(options.Addon, "configs/translations"));
            return StageResult::Ok(Engine().Translations.GetLanguage());
        });
    }
    LoadPluginManifest(options.Addon);
    return true;
}

}  // namespace CS2Kit::Core
