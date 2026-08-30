#pragma once

#include <VoltMod/App/PluginSettings.hpp>
#include <VoltMod/Core/LoadReport.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Runtime.hpp>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

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
 * @brief The standard OnLoad prelude, recorded as LoadReport stages.
 *
 * "Configuration" loads addons/<Addon>/<SettingsFile>, via TConfig::LoadSettings when it
 * satisfies @ref HasLoadSettings (the load-then-validate convention) and JsonConfig::Load
 * otherwise; a failed stage aborts the load and reports the parse error. "Translations"
 * applies `plugin.locale` when the settings root satisfies @ref HasPluginSection - and logs
 * that it did not when it does not - then loads addons/<Addon>/configs/translations.
 */
template <class TConfig>
bool LoadStandardConfig(Runtime& runtime, TConfig& config, const StandardLoadOptions& options)
{
    auto& report = runtime.LoadReport;
    const std::string path = AddonFile(options.Addon, options.SettingsFile);

    const auto status = report.Run("Configuration", [&] {
        const Status loaded = [&] {
            if constexpr (HasLoadSettings<TConfig>)
                return config.LoadSettings(path);
            else
                return config.Load(path);
        }();
        // The detail is the parse error itself - line, column and the offending key - because
        // "failed to load <path>" is not enough to find a misspelled setting.
        return loaded ? StageResult::Ok(path) : StageResult::Failed(std::format("{}: {}", path, loaded.error().Detail));
    });
    if (status == StageStatus::Failed)
        return false;

    if (options.Translations)
    {
        auto& translations = runtime.Translations;
        report.Run("Translations", [&] {
            using SettingsType = std::remove_cvref_t<decltype(config.Get())>;
            if constexpr (HasPluginSection<SettingsType>)
            {
                translations.SetLanguage(config.Get().plugin.locale);
            }
            else
            {
                // Say so rather than vanishing: a root that never grew a `plugin` section reads
                // as "translations work" until someone notices the locale was never applied.
                Log::Info("Settings carry no `plugin` section; keeping locale {}.", translations.GetLanguage());
            }
            translations.Load(AddonFile(options.Addon, "configs/translations"));
            return StageResult::Ok(translations.GetLanguage());
        });
    }
    return true;
}

}  // namespace VoltMod
