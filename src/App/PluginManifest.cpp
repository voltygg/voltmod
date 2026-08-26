#include <VoltMod/App/PluginManifest.hpp>
#include <algorithm>
#include <charconv>
#include <nlohmann/json.hpp>

namespace VoltMod
{

/** Numeric components of a version, stopping at the first `+build` or `-suffix`. */
static std::vector<int> VersionParts(std::string_view version)
{
    version = version.substr(0, std::min(version.find('+'), version.find('-')));

    std::vector<int> parts;
    while (!version.empty())
    {
        const size_t dot = version.find('.');
        const std::string_view field = version.substr(0, dot);

        int value = 0;
        const auto [ptr, ec] = std::from_chars(field.data(), field.data() + field.size(), value);
        // Stop rather than read a non-numeric component as zero: "1.x" is not "1.0".
        if (ec != std::errc{} || ptr != field.data() + field.size())
            break;
        parts.push_back(value);

        if (dot == std::string_view::npos)
            break;
        version.remove_prefix(dot + 1);
    }
    return parts;
}

bool VersionAtLeast(std::string_view have, std::string_view want)
{
    if (want.empty())
        return true;

    const std::vector<int> lhs = VersionParts(have);
    const std::vector<int> rhs = VersionParts(want);
    if (lhs.empty())
        return false;

    for (size_t i = 0; i < std::max(lhs.size(), rhs.size()); ++i)
    {
        const int a = i < lhs.size() ? lhs[i] : 0;
        const int b = i < rhs.size() ? rhs[i] : 0;
        if (a != b)
            return a > b;
    }
    return true;
}

std::string IdentityKey(std::string_view pluginName)
{
    return "voltmod.IPluginIdentity/1:" + std::string(pluginName);
}

std::optional<PluginManifest> ParsePluginManifest(std::string_view json)
{
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object())
        return std::nullopt;

    PluginManifest manifest;
    manifest.Name = root.value("name", std::string{});
    manifest.Version = root.value("version", std::string{});
    manifest.Description = root.value("description", std::string{});
    // A manifest that cannot say who it is describes nothing; the rest is optional.
    if (manifest.Name.empty())
        return std::nullopt;

    const auto deps = root.find("dependencies");
    if (deps == root.end())
        return manifest;
    if (!deps->is_array())
        return std::nullopt;

    for (const auto& entry : *deps)
    {
        if (!entry.is_object())
            return std::nullopt;

        PluginDependency dependency;
        dependency.Name = entry.value("name", std::string{});
        dependency.MinVersion = entry.value("minVersion", std::string{});
        dependency.Required = entry.value("required", false);
        if (dependency.Name.empty())
            return std::nullopt;
        manifest.Dependencies.push_back(std::move(dependency));
    }
    return manifest;
}

}  // namespace VoltMod
