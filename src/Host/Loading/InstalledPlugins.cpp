#include "Host/Loading/InstalledPlugins.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/EnumNames.hpp>
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
    std::string website;
    std::string license;
    std::string logLevel = "info";
    std::vector<std::string> dependencies;
    std::vector<std::string> optionalDependencies;
    // Read by `voltmod database header`; the host only accepts it.
    std::optional<glz::raw_json> database;
    // Points editors at the plugin schema; the host only accepts it.
    std::optional<std::string> schema;
};

}  // namespace VoltMod

template <>
struct glz::meta<VoltMod::PluginDocument>
{
    static constexpr std::string_view rename_key(std::string_view key) { return key == "schema" ? "$schema" : key; }
};

namespace VoltMod
{

static constexpr std::string_view ManifestName = "plugin.json";

Discovered InstalledPlugins::Discover(const std::filesystem::path& plugins)
{
    Discovered installed;

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
            installed.Refused.push_back({.Name = directory, .Reason = document.error()});
            continue;
        }

        if (document->name != directory)
        {
            installed.Refused.push_back({.Name = directory,
                                         .Reason = Error::Invalid(std::format(
                                             "{} names it '{}', and a plugin lives in the directory it is named after.",
                                             ManifestName, document->name))});
            continue;
        }

        const std::optional<LogLevel> level = Parse<LogLevel>(document->logLevel);
        if (!level)
        {
            installed.Refused.push_back(
                {.Name = directory,
                 .Reason = Error::Invalid(
                     std::format("{}: logLevel '{}' is not info, warn or error", ManifestName, document->logLevel))});
            continue;
        }

        installed.Plugins.push_back({.Name = document->name,
                                     .Version = document->version,
                                     .LogTag = document->logTag.empty() ? document->name : document->logTag,
                                     .Description = document->description,
                                     .Author = document->author,
                                     .Website = document->website,
                                     .License = document->license,
                                     .LogLevel = *level,
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
