#include "Host/Loading/PluginDependencies.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace VoltMod
{

using PluginsByName = std::map<std::string, const PluginManifest*, std::less<>>;
using RefusalsByName = std::map<std::string, Error, std::less<>>;

// Refuse whoever names a dependency that is not installed, then whoever required them, and so on
// down the chain. Plugins already in @p refused seed the chain.
static void RefuseUnsatisfied(const PluginsByName& plugins, RefusalsByName& refused)
{
    std::vector<std::string> spreading;
    for (const auto& [name, reason] : refused)
    {
        spreading.push_back(name);
    }

    const auto refuse = [&](const std::string& name, Error reason) {
        if (refused.emplace(name, std::move(reason)).second)
        {
            spreading.push_back(name);
        }
    };

    for (const auto& [name, manifest] : plugins)
    {
        for (const std::string& dependency : manifest->Dependencies)
        {
            if (!plugins.contains(dependency))
            {
                refuse(name, Error::NotFound(std::format("requires '{}', which is not installed", dependency)));
                break;
            }
        }
    }

    while (!spreading.empty())
    {
        const std::string gone = std::move(spreading.back());
        spreading.pop_back();

        for (const auto& [name, manifest] : plugins)
        {
            if (!refused.contains(name) && std::ranges::contains(manifest->Dependencies, gone))
            {
                refuse(name, Error::NotReady(std::format("requires '{}', which the host refused", gone)));
            }
        }
    }
}

LoadList PluginDependencies::Resolve(std::span<const PluginManifest> installed)
{
    PluginsByName plugins;
    RefusalsByName refused;
    for (const PluginManifest& manifest : installed)
    {
        if (!plugins.emplace(manifest.Name, &manifest).second)
        {
            refused.emplace(manifest.Name, Error::Invalid("installed more than once; each plugin directory needs its "
                                                          "own plugin name"));
        }
    }

    RefuseUnsatisfied(plugins, refused);

    LoadList list;
    for (const auto& [name, manifest] : plugins)
    {
        if (!refused.contains(name))
        {
            list.Allowed.push_back(name);
        }
    }

    for (auto& [name, reason] : refused)
    {
        list.Refused.push_back({name, std::move(reason)});
    }

    return list;
}

std::vector<std::string> PluginDependencies::RequiredDependents(std::string_view plugin,
                                                                std::span<const PluginManifest> loaded)
{
    std::set<std::string, std::less<>> dependents;
    std::vector<std::string> spreading{std::string(plugin)};
    while (!spreading.empty())
    {
        const std::string needed = std::move(spreading.back());
        spreading.pop_back();

        for (const PluginManifest& manifest : loaded)
        {
            if (manifest.Name != plugin && !dependents.contains(manifest.Name) &&
                std::ranges::contains(manifest.Dependencies, needed))
            {
                dependents.insert(manifest.Name);
                spreading.push_back(manifest.Name);
            }
        }
    }

    return {dependents.begin(), dependents.end()};
}

}  // namespace VoltMod
