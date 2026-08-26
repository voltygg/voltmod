#pragma once

#include <VoltMod/App/PluginManifest.hpp>
#include <VoltMod/Core/LoadReport.hpp>
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

/** Adopt addons/<addon>/<addon>.manifest.json as a LoadReport stage. Absent or malformed
 *  degrades the stage rather than failing the load: the manifest is diagnostics. */
void LoadPluginManifest(Runtime& runtime, std::string_view addon);

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
bool LoadStandardConfig(Runtime& runtime, TConfig& config, const StandardLoadOptions& options)
{
    auto& report = runtime.LoadReport;
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
        auto& translations = runtime.Translations;
        report.Run("Translations", [&] {
            if constexpr (requires { translations.SetLanguage(config.Get().plugin.locale); })
                translations.SetLanguage(config.Get().plugin.locale);
            translations.Load(AddonFile(options.Addon, "configs/translations"));
            return StageResult::Ok(translations.GetLanguage());
        });
    }
    LoadPluginManifest(runtime, options.Addon);
    return true;
}

}  // namespace VoltMod
