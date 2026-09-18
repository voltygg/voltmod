#include "Host/PluginOrder.hpp"

#include <VoltMod/Core/Text/Strings.hpp>
#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace VoltMod
{

using NameSet = std::set<std::string, std::less<>>;
using PluginsByName = std::map<std::string, const PluginManifest*, std::less<>>;
using RefusalsByName = std::map<std::string, Error, std::less<>>;
using DependencyEdges = std::map<std::string, std::vector<std::string>, std::less<>>;

// Walk states for the cycle search.
static constexpr int Unvisited = 0;
static constexpr int OnStack = 1;
static constexpr int Finished = 2;

static Error InstalledTwice()
{
    return Error::Invalid("installed more than once; each addon directory needs its own plugin name");
}

static Error MissingDependency(std::string_view dependency)
{
    return Error::NotFound(std::format("requires '{}', which is not installed", dependency));
}

static Error RefusedDependency(std::string_view dependency)
{
    return Error::NotReady(std::format("requires '{}', which the host refused", dependency));
}

static Error CycleFound(const std::vector<std::string>& cycle)
{
    return Error::Invalid(std::format("dependency cycle: {}", Strings::Join(cycle, " -> ")));
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
                    refused.emplace(name, MissingDependency(dependency));
                    changed = true;
                    break;
                }
                if (refused.contains(dependency))
                {
                    refused.emplace(name, RefusedDependency(dependency));
                    changed = true;
                    break;
                }
            }
        }
    }
}

// Edges of the plugins still standing: every dependency, required or optional, that will load too.
static DependencyEdges BuildEdges(const PluginsByName& installed, const RefusalsByName& refused)
{
    const auto stillStanding = [&](const std::string& name) {
        return installed.contains(name) && !refused.contains(name);
    };

    DependencyEdges edges;
    for (const auto& [name, manifest] : installed)
    {
        if (refused.contains(name))
            continue;

        std::vector<std::string>& dependencies = edges[name];
        for (const std::string& dependency : manifest->Dependencies)
            if (stillStanding(dependency))
                dependencies.push_back(dependency);

        for (const std::string& dependency : manifest->OptionalDependencies)
            if (stillStanding(dependency))
                dependencies.push_back(dependency);
    }
    return edges;
}

static bool Walk(const DependencyEdges& edges, const std::string& name, std::map<std::string, int, std::less<>>& state,
                 std::vector<std::string>& path, std::vector<std::string>& cycle)
{
    state[name] = OnStack;
    path.push_back(name);

    for (const std::string& dependency : edges.at(name))
    {
        if (state[dependency] == OnStack)
        {
            // Close the loop so the reason reads a -> b -> a.
            cycle.assign(std::find(path.begin(), path.end(), dependency), path.end());
            cycle.push_back(dependency);
            return true;
        }
        if (state[dependency] == Unvisited && Walk(edges, dependency, state, path, cycle))
            return true;
    }

    path.pop_back();
    state[name] = Finished;
    return false;
}

// One cycle, named from where it enters back to itself, or empty when the graph is acyclic.
static std::vector<std::string> FindCycle(const DependencyEdges& edges)
{
    std::map<std::string, int, std::less<>> state;
    std::vector<std::string> path;
    std::vector<std::string> cycle;

    for (const auto& [name, dependencies] : edges)
        if (state[name] == Unvisited && Walk(edges, name, state, path, cycle))
            break;

    return cycle;
}

std::vector<std::string> LoadPlan::UnloadOrder() const
{
    return {Load.rbegin(), Load.rend()};
}

LoadPlan PluginOrder::Plan(std::span<const PluginManifest> installed)
{
    PluginsByName plugins;
    RefusalsByName refused;
    for (const PluginManifest& manifest : installed)
        if (!plugins.emplace(manifest.Name, &manifest).second)
            refused.insert_or_assign(manifest.Name, InstalledTwice());

    RefuseUnsatisfied(plugins, refused);

    DependencyEdges edges = BuildEdges(plugins, refused);
    while (true)
    {
        const std::vector<std::string> cycle = FindCycle(edges);
        if (cycle.empty())
            break;

        // The cycle is the better reason, so it reaches its members before the chain refusal does.
        const Error reason = CycleFound(cycle);
        for (const std::string& name : cycle)
            refused.emplace(name, reason);

        RefuseUnsatisfied(plugins, refused);
        edges = BuildEdges(plugins, refused);
    }

    LoadPlan plan;
    NameSet loaded;
    while (plan.Load.size() < edges.size())
    {
        const size_t before = plan.Load.size();
        for (const auto& [name, dependencies] : edges)
        {
            const auto isLoaded = [&](const std::string& dependency) { return loaded.contains(dependency); };
            if (!loaded.contains(name) && std::ranges::all_of(dependencies, isLoaded))
            {
                plan.Load.push_back(name);
                loaded.insert(name);
                break;
            }
        }
        // Unreachable once cycles are refused; it is here so a bad graph cannot spin the game thread.
        if (plan.Load.size() == before)
            break;
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

}  // namespace VoltMod
