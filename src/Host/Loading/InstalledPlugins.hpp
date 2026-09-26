#pragma once

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Host/PluginDescriptor.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace VoltMod
{

/** One installed plugin manifest, already parsed. Reading the file is @ref InstalledPlugins' job. */
struct PluginManifest
{
    std::string Name;
    std::string Version;
    std::string LogTag;  ///< prefixes the plugin's log lines; its name unless the file says otherwise
    std::string Description;
    std::string Author;
    std::string Website;
    std::string License;
    /** The level the plugin starts at; `volt log` changes it until the next load. */
    VoltMod::LogLevel LogLevel = VoltMod::LogLevel::Info;
    std::vector<std::string> Dependencies;  ///< Required: a missing one refuses this plugin.
    /** Wanted, not needed: the plugin loads without these and reaches them through the exchange if they came. */
    std::vector<std::string> OptionalDependencies;
};

/** A plugin the host will not load, and the reason its log line gives. */
struct RefusedPlugin
{
    std::string Name;
    Error Reason;
};

/** What the plugins directory holds: the manifests that parsed, and the ones that did not. */
struct Discovered
{
    std::vector<PluginManifest> Plugins;
    std::vector<RefusedPlugin> Refused;
};

/** Reading the installed plugin directory. No SDK, so it is tested against a real one. */
namespace InstalledPlugins
{

/**
 * @brief Every `<plugins>/<name>/plugin.json`.
 *
 * A directory without a manifest is not a VoltMod plugin and is passed over in silence. One whose
 * manifest is malformed, or names a plugin other than its directory, is refused with the reason.
 */
Discovered Discover(const std::filesystem::path& plugins);

}  // namespace InstalledPlugins

/** Whether @p descriptor is one this host can load, with the reason it is not. */
Status ValidateDescriptor(const PluginDescriptor* descriptor);

}  // namespace VoltMod
