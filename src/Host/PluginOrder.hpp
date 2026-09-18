#pragma once

#include <VoltMod/Core/Result.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** One `addons/<name>/plugin.json`, already parsed. Reading the file is the caller's job. */
struct PluginManifest
{
    std::string Name;
    std::vector<std::string> Dependencies;          ///< Required: a missing one refuses this plugin.
    std::vector<std::string> OptionalDependencies;  ///< Order after these when installed, ignore them when not.
};

/** A plugin the host will not load, and the reason its log line gives. */
struct RefusedPlugin
{
    std::string Name;
    Error Reason;
};

/** What the host loads, in order, and what it leaves out. */
struct LoadPlan
{
    std::vector<std::string> Load;       ///< Dependencies first, ties alphabetical.
    std::vector<RefusedPlugin> Refused;  ///< Alphabetical by name.

    /** Teardown order: the exact reverse of @ref Load. */
    std::vector<std::string> UnloadOrder() const;
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
 * Required dependencies only: this is what `volt unload` refuses for and what `volt reload` takes
 * with it. Alphabetical; a caller that needs them in load order runs them through @ref Plan.
 */
std::vector<std::string> RequiredDependents(std::string_view plugin, std::span<const PluginManifest> loaded);

}  // namespace PluginOrder
}  // namespace VoltMod
