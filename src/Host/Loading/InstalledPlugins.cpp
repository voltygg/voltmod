#include "Host/Loading/InstalledPlugins.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <VoltMod/Host/Abi.hpp>
#include <format>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace VoltMod
{

/** The shape of plugin.json: the member names are its keys, and an unknown key is an error. */
struct PluginDocument
{
    std::string name;
    std::string version;
    std::string logTag;
    std::string description;
    std::string author;
    std::vector<std::string> dependencies;
    std::vector<std::string> optionalDependencies;
    // Read by `voltmod database header`; the host only accepts it.
    std::optional<glz::raw_json> database;
};

static constexpr std::string_view ManifestName = "plugin.json";

std::vector<PluginManifest> InstalledPlugins::Discover(const std::filesystem::path& plugins)
{
    std::vector<PluginManifest> installed;

    std::error_code failed;
    if (!std::filesystem::exists(plugins, failed))
    {
        if (failed)
        {
            Log::Error("Cannot inspect {}: {}", plugins.string(), failed.message());
        }
        return installed;
    }

    for (std::filesystem::directory_iterator entry(plugins, failed), end; !failed && entry != end;
         entry.increment(failed))
    {
        if (!entry->is_directory(failed) || failed)
        {
            continue;
        }

        const std::string directory = entry->path().filename().string();
        const std::filesystem::path manifest = entry->path() / ManifestName;
        if (!std::filesystem::exists(manifest, failed) || failed)
        {
            continue;
        }

        const Result<PluginDocument> document =
            Json::ReadFile<PluginDocument, Json::StrictReadOptions>(manifest.string());
        if (!document)
        {
            Log::Error("Refusing '{}': {}", directory, document.error().Detail);
            continue;
        }

        if (document->name != directory)
        {
            Log::Error("Refusing '{}': {} names it '{}', and a plugin lives in the directory it is named after.",
                       directory, ManifestName, document->name);
            continue;
        }

        installed.push_back({.Name = document->name,
                             .Version = document->version,
                             .LogTag = document->logTag.empty() ? document->name : document->logTag,
                             .Description = document->description,
                             .Author = document->author,
                             .Dependencies = document->dependencies,
                             .OptionalDependencies = document->optionalDependencies});
    }

    if (failed)
    {
        Log::Error("Cannot read {}: {}", plugins.string(), failed.message());
    }

    return installed;
}

Status ValidateDescriptor(const PluginDescriptor* descriptor)
{
    if (descriptor == nullptr)
    {
        return std::unexpected(Error::Invalid(std::format("{} returned nothing", PluginEntryName)));
    }

    if (descriptor->AbiVersion != HostAbiVersion)
    {
        return std::unexpected(Error::Invalid(
            std::format("it was built against host ABI version {} and this host speaks version {}; rebuild the "
                        "plugin against this VoltMod",
                        descriptor->AbiVersion, HostAbiVersion)));
    }

    if (descriptor->Load == nullptr || descriptor->Unload == nullptr || descriptor->Status == nullptr)
    {
        return std::unexpected(Error::Invalid("its descriptor leaves out one of Load, Unload and Status"));
    }

    return {};
}

}  // namespace VoltMod
