#include "Host/Loading/PluginOrder.hpp"

#include <VoltMod/Core/Text/Strings.hpp>
#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <ranges>
#include <set>
#include <utility>

namespace VoltMod
{

using NameSet = std::set<std::string, std::less<>>;
using PluginsByName = std::map<std::string, const PluginManifest*, std::less<>>;
using RefusalsByName = std::map<std::string, Error, std::less<>>;

/** Everything a plugin waits for that is still standing: both dependency kinds, minus what is gone. */
static std::vector<std::string> Waits(const PluginManifest& manifest, const PluginsByName& installed,
                                      const RefusalsByName& refused)
{
    std::vector<std::string> waits;
    for (const std::vector<std::string>* kind : {&manifest.Dependencies, &manifest.OptionalDependencies})
        for (const std::string& dependency : *kind)
            if (installed.contains(dependency) && !refused.contains(dependency))
                waits.push_back(dependency);

    return waits;
}

// Refuse every plugin whose required dependencies are missing or refused, and so on up the chain.
static void RefuseUnsatisfied(const PluginsByName& installed, RefusalsByName& refused)
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto& [name, manifest] : installed)
        {
            if (refused.contains(name))
                continue;

            for (const std::string& dependency : manifest->Dependencies)
            {
                if (!installed.contains(dependency))
                {
                    refused.emplace(name,
                                    Error::NotFound(std::format("requires '{}', which is not installed", dependency)));
                    changed = true;
                    break;
                }
                if (refused.contains(dependency))
                {
                    refused.emplace(name,
                                    Error::NotReady(std::format("requires '{}', which the host refused", dependency)));
                    changed = true;
                    break;
                }
            }
        }
    }
}

// Follow the waits from @p name until one comes back to a plugin already on the path.
static bool Walk(const std::string& name, const PluginsByName& installed, const RefusalsByName& refused,
                 const NameSet& ordered, std::map<std::string, bool, std::less<>>& onPath,
                 std::vector<std::string>& path, std::vector<std::string>& cycle)
{
    onPath[name] = true;
    path.push_back(name);

    for (const std::string& dependency : Waits(*installed.at(name), installed, refused))
    {
        if (ordered.contains(dependency))
            continue;
        if (const auto walked = onPath.find(dependency); walked != onPath.end())
        {
            if (!walked->second)
                continue;

            // Close the loop so the reason reads a -> b -> a.
            cycle.assign(std::ranges::find(path, dependency), path.end());
            cycle.push_back(dependency);
            return true;
        }
        if (Walk(dependency, installed, refused, ordered, onPath, path, cycle))
            return true;
    }

    path.pop_back();
    onPath[name] = false;
    return false;
}

LoadPlan PluginOrder::Plan(std::span<const PluginManifest> installed)
{
    PluginsByName plugins;
    RefusalsByName refused;
    for (const PluginManifest& manifest : installed)
        if (!plugins.emplace(manifest.Name, &manifest).second)
            refused.insert_or_assign(manifest.Name,
                                     Error::Invalid("installed more than once; each plugin directory needs its "
                                                    "own plugin name"));

    RefuseUnsatisfied(plugins, refused);

    LoadPlan plan;
    NameSet ordered;
    while (true)
    {
        // Of the plugins that are ready the alphabetically first goes next, until only cycles are left.
        for (bool emitted = true; emitted;)
        {
            emitted = false;
            for (const auto& [name, manifest] : plugins)
            {
                if (ordered.contains(name) || refused.contains(name))
                    continue;

                const std::vector<std::string> waits = Waits(*manifest, plugins, refused);
                if (!std::ranges::all_of(waits, [&](const std::string& it) { return ordered.contains(it); }))
                    continue;

                plan.Order.push_back(name);
                ordered.insert(name);
                emitted = true;
                break;
            }
        }

        std::vector<std::string> cycle;
        std::map<std::string, bool, std::less<>> onPath;
        std::vector<std::string> path;
        for (const auto& [name, manifest] : plugins)
            if (!ordered.contains(name) && !refused.contains(name) && !onPath.contains(name) &&
                Walk(name, plugins, refused, ordered, onPath, path, cycle))
                break;

        if (cycle.empty())
            break;

        // The cycle is the better reason, so it reaches its members before the chain refusal does.
        const Error reason = Error::Invalid(std::format("dependency cycle: {}", Strings::Join(cycle, " -> ")));
        for (const std::string& name : cycle)
            refused.emplace(name, reason);
        RefuseUnsatisfied(plugins, refused);
    }

    for (auto& [name, reason] : refused)
        plan.Refused.push_back({name, std::move(reason)});

    return plan;
}

std::vector<std::string> PluginOrder::RequiredDependents(std::string_view plugin,
                                                         std::span<const PluginManifest> loaded)
{
    NameSet dependents{std::string(plugin)};
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const PluginManifest& manifest : loaded)
        {
            if (dependents.contains(manifest.Name))
                continue;

            const auto isDependent = [&](const std::string& dependency) { return dependents.contains(dependency); };
            if (std::ranges::any_of(manifest.Dependencies, isDependent))
            {
                dependents.insert(manifest.Name);
                changed = true;
            }
        }
    }

    dependents.erase(std::string(plugin));
    return {dependents.begin(), dependents.end()};
}

std::vector<std::string> PluginOrder::ReloadGroup(std::string_view plugin, std::span<const PluginManifest> loaded)
{
    NameSet group;
    for (const std::string& dependent : RequiredDependents(plugin, loaded))
        group.insert(dependent);
    group.emplace(plugin);

    std::vector<std::string> going;
    for (const PluginManifest& manifest : loaded | std::views::reverse)
        if (group.contains(manifest.Name))
            going.push_back(manifest.Name);

    return going;
}

}  // namespace VoltMod
