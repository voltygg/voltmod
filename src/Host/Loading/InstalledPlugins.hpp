#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Host/PluginDescriptor.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace VoltMod
{

/** One `addons/<name>/plugin.json`, already parsed. Reading the file is @ref InstalledPlugins' job. */
struct PluginManifest
{
    std::string Name;
    std::string Version;
    std::string LogTag;  ///< prefixes the plugin's log lines; its name unless the file says otherwise
    std::string Description;
    std::string Author;
    std::vector<std::string> Dependencies;          ///< Required: a missing one refuses this plugin.
    std::vector<std::string> OptionalDependencies;  ///< Order after these when installed, ignore them when not.
};

/** Reading what is installed under an addons directory. No SDK, so it is tested against a real one. */
namespace InstalledPlugins
{

/**
 * @brief Every `<addons>/<name>/plugin.json` that parsed.
 *
 * A directory without a manifest is not a VoltMod plugin and is passed over in silence. One whose
 * manifest is malformed, or names a plugin other than its directory, is logged and left out.
 */
std::vector<PluginManifest> Discover(const std::filesystem::path& addons);

}  // namespace InstalledPlugins

/** Whether @p descriptor is one this host can load, with the reason it is not. */
Status ValidateDescriptor(const PluginDescriptor* descriptor);

}  // namespace VoltMod
