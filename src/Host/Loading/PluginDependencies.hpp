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

/** Which plugins the host loads and which it turns away, with the reason. Both alphabetical. */
struct LoadList
{
    std::vector<std::string> Allowed;
    std::vector<RefusedPlugin> Refused;
};

/** Reading the dependency lists: who may load, and who needs whom. Pure: no file, no log, no SDK. */
namespace PluginDependencies
{

/**
 * @brief Decide which of @p installed the host loads.
 *
 * A required dependency that is missing or itself refused refuses the plugin naming it, and so on
 * down the chain; an optional dependency never refuses anything. Load order is alphabetical and
 * means nothing: a plugin reaches another through @ref VoltMod::ServiceExchange when it needs it,
 * which is a call, not a load-time edge.
 */
LoadList Resolve(std::span<const PluginManifest> installed);

/**
 * @brief The plugins in @p loaded that need @p plugin, directly or through another plugin.
 *
 * Required dependencies only: this is what `volt unload` refuses for and what `volt reload` takes
 * down with the plugin. Alphabetical.
 */
std::vector<std::string> RequiredDependents(std::string_view plugin, std::span<const PluginManifest> loaded);

}  // namespace PluginDependencies
}  // namespace VoltMod
