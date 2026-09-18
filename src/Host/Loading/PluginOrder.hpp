#pragma once

#include "Host/Loading/InstalledPlugins.hpp"

#include <VoltMod/Core/Result.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** A plugin the host will not load, and the reason its log line gives. */
struct RefusedPlugin
{
    std::string Name;
    Error Reason;
};

/** What the host loads, in order, and what it leaves out. */
struct LoadPlan
{
    std::vector<std::string> Order;      ///< Dependencies first, ties alphabetical.
    std::vector<RefusedPlugin> Refused;  ///< Alphabetical by name.
};

/** Load ordering over plugin manifests. Pure: no file, no log, no SDK. */
namespace PluginOrder
{

/**
 * @brief Decide the order @p installed loads in.
 *
 * A plugin loads after every dependency it names that is installed, required or optional; an
 * optional dependency that is not installed is ignored. A required dependency that is missing or
 * itself refused refuses the plugin, and so on up the chain. A cycle refuses every plugin in it,
 * whether the edges are required or optional, since neither order works. Of the plugins whose
 * dependencies are already loaded the alphabetically first goes next, so the plan is stable.
 */
LoadPlan Plan(std::span<const PluginManifest> installed);

/**
 * @brief The plugins in @p loaded that need @p plugin, directly or through another plugin.
 *
 * Required dependencies only: this is what `volt unload` refuses for. Alphabetical; a caller that
 * needs them in load order runs them through @ref Plan.
 */
std::vector<std::string> RequiredDependents(std::string_view plugin, std::span<const PluginManifest> loaded);

/**
 * @brief What `volt reload <plugin>` takes down with it, newest first.
 *
 * @p loaded is in load order, so the answer is the unload order: @p plugin and everything that
 * requires it, each one after the plugins that require it.
 */
std::vector<std::string> ReloadGroup(std::string_view plugin, std::span<const PluginManifest> loaded);

}  // namespace PluginOrder
}  // namespace VoltMod
