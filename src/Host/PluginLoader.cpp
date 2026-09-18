#include "Host/PluginLoader.hpp"

#include "Host/PluginHost.hpp"

#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/EnumNames.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <VoltMod/Host/Abi.hpp>
#include <algorithm>
#include <format>
#include <optional>
#include <system_error>
#include <tier1/convar.h>
#include <utility>

namespace VoltMod
{

#if defined(_WIN32)
static constexpr std::string_view PlatformDirectory = "win64";
static constexpr std::string_view LibrarySuffix = ".dll";
#else
static constexpr std::string_view PlatformDirectory = "linuxsteamrt64";
static constexpr std::string_view LibrarySuffix = ".so";
#endif

static constexpr std::string_view ManifestName = "plugin.json";
static constexpr std::string_view CommandUsage =
    "volt list | status [name] | load <name> | unload <name> | reload <name> | "
    "log <name> <info|warn|error>";

/** The shape of plugin.json: the member names are its keys. */
struct PluginDocument
{
    std::string name;
    std::string version;
    std::vector<std::string> dependencies;
    std::vector<std::string> optionalDependencies;
};

// Whatever a plugin still held when its context closed is a bug in that plugin; the host has
// already dropped it by the time this runs.
static void ReportLeaks(std::string_view name, const PluginLeaks& leaks)
{
    for (HostEvent event : leaks.Subscriptions)
        Log::Warn("{} left a {} subscription behind; the host dropped it.", name, Name(event));
    for (const std::string& service : leaks.Services)
        Log::Warn("{} left the service '{}' published; the host withdrew it.", name, service);
    for (const std::string& command : leaks.Commands)
        Log::Warn("{} left the command name '{}' claimed; the host released it.", name, command);
}

PluginLoader::PluginLoader(PluginHost& host) : _host(host) {}

PluginLoader::~PluginLoader()
{
    Stop();
}

void PluginLoader::Start()
{
    // Reserve the name before any plugin loads, so a plugin registering `volt` is refused with
    // the usual conflict line rather than fighting the host for the tier1 command.
    _host.ReserveCommand("volt");
    _command = std::make_unique<ServerCommand>("volt", CommandUsage,
                                               [this](const CCommand& arguments) { RunCommand(arguments); });

    const std::vector<InstalledPlugin> installed = Discover();

    std::vector<PluginManifest> manifests;
    manifests.reserve(installed.size());
    for (const InstalledPlugin& plugin : installed)
        manifests.push_back(plugin.Manifest);

    const LoadPlan plan = PluginOrder::Plan(manifests);
    for (const RefusedPlugin& refused : plan.Refused)
        Log::Error("Refusing '{}': {}", refused.Name, refused.Reason.Detail);

    for (const std::string& name : plan.Load)
    {
        const auto found = std::ranges::find_if(
            installed, [&](const InstalledPlugin& plugin) { return plugin.Manifest.Name == name; });
        if (Status loaded = LoadOne(*found); !loaded)
            Log::Error("Refusing '{}': {}", name, loaded.error().Detail);
    }

    Log::Info("{} of {} installed plugin(s) loaded.", _loaded.size(), installed.size());
}

void PluginLoader::Stop()
{
    _queue.clear();
    while (!_loaded.empty())
        UnloadOne(_loaded.back().Manifest.Name);
    _command.reset();
}

void PluginLoader::RunQueuedRequests()
{
    if (_queue.empty())
        return;

    // Take the queue aside: a request that queues another leaves it for the next frame.
    const std::vector<Request> requests = std::move(_queue);
    _queue.clear();

    for (const Request& request : requests)
    {
        switch (request.Kind)
        {
        case RequestKind::Load:
            RunLoad(request.Name);
            break;
        case RequestKind::Unload:
            RunUnload(request.Name);
            break;
        case RequestKind::Reload:
            RunReload(request.Name);
            break;
        }
    }
}

std::vector<PluginLoader::InstalledPlugin> PluginLoader::Discover()
{
    std::vector<InstalledPlugin> installed;

    const std::filesystem::path addons = ResolvePath("addons");
    std::error_code failed;
    for (std::filesystem::directory_iterator entry(addons, failed), end; !failed && entry != end;
         entry.increment(failed))
    {
        if (!entry->is_directory(failed) || failed)
            continue;

        const std::string directory = entry->path().filename().string();
        const std::string manifest = AddonFile(directory, ManifestName);
        if (!std::filesystem::exists(ResolvePath(manifest), failed) || failed)
            continue;  // an addon that is not a VoltMod plugin

        const Result<PluginDocument> document = Json::ReadFile<PluginDocument>(manifest);
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

        installed.push_back({.Manifest = {.Name = document->name,
                                          .Dependencies = document->dependencies,
                                          .OptionalDependencies = document->optionalDependencies},
                             .Version = document->version});
    }

    if (failed)
        Log::Error("Cannot read {}: {}", addons.string(), failed.message());

    return installed;
}

std::filesystem::path PluginLoader::LibraryPath(std::string_view name)
{
    return ResolvePath(AddonFile(name, std::format("bin/{}/{}{}", PlatformDirectory, name, LibrarySuffix)));
}

Status PluginLoader::LoadOne(const InstalledPlugin& plugin)
{
    const std::string& name = plugin.Manifest.Name;

    Result<Library> code = Library::Open(LibraryPath(name));
    if (!code)
        return std::unexpected(code.error());

    const Result<void*> entry = code->Symbol(PluginEntryName);
    if (!entry)
        return std::unexpected(entry.error());

    using EntryPoint = const PluginDescriptor* (*)();
    const PluginDescriptor* descriptor = reinterpret_cast<EntryPoint>(*entry)();
    if (descriptor == nullptr)
        return std::unexpected(Error::Invalid(std::format("{} returned nothing", PluginEntryName)));

    if (descriptor->AbiVersion != HostAbiVersion)
        return std::unexpected(Error::Invalid(
            std::format("it was built against host ABI version {} and this host speaks version {}; rebuild the "
                        "plugin against this VoltMod",
                        descriptor->AbiVersion, HostAbiVersion)));

    if (descriptor->Load == nullptr || descriptor->Unload == nullptr || descriptor->Status == nullptr)
        return std::unexpected(Error::Invalid("its descriptor leaves out one of Load, Unload and Status"));

    PluginContext* context = _host.OpenPlugin(name);
    if (context == nullptr)
        return std::unexpected(Error::Failed("the host already holds a context under that name"));

    char failure[512] = {};
    if (!descriptor->Load(context, failure, sizeof failure))
    {
        failure[sizeof failure - 1] = '\0';
        // A plugin that refuses its own load has already torn itself down, so the host only lets
        // go of what it took; the library is freed as `code` leaves this scope.
        ReportLeaks(name, _host.ClosePlugin(name));
        return std::unexpected(Error::Failed(failure[0] != '\0' ? failure : "its Load returned false"));
    }

    _loaded.push_back(
        {.Manifest = plugin.Manifest, .Version = plugin.Version, .Descriptor = descriptor, .Code = std::move(*code)});

    Log::Info("Loaded {} v{}.", name, plugin.Version);
    return {};
}

void PluginLoader::UnloadOne(std::string_view name)
{
    // Copy it: the view may point into the record this is about to erase.
    const std::string plugin(name);

    const auto found =
        std::ranges::find_if(_loaded, [&](const LoadedPlugin& loaded) { return loaded.Manifest.Name == plugin; });
    if (found == _loaded.end())
        return;

    found->Descriptor->Unload();
    ReportLeaks(plugin, _host.ClosePlugin(plugin));

    // Only now is the library freed: every hook thunk and subscription closure it installed is
    // code inside it, and Unload is what takes them down.
    _loaded.erase(found);
    Log::Info("Unloaded {}.", plugin);
}

PluginLoader::LoadedPlugin* PluginLoader::FindLoaded(std::string_view name)
{
    const auto found =
        std::ranges::find_if(_loaded, [&](const LoadedPlugin& plugin) { return plugin.Manifest.Name == name; });
    return found != _loaded.end() ? &*found : nullptr;
}

std::vector<PluginManifest> PluginLoader::LoadedManifests() const
{
    std::vector<PluginManifest> manifests;
    manifests.reserve(_loaded.size());
    for (const LoadedPlugin& plugin : _loaded)
        manifests.push_back(plugin.Manifest);
    return manifests;
}

void PluginLoader::RunCommand(const CCommand& arguments)
{
    const std::string_view verb = arguments.ArgC() >= 2 ? arguments.Arg(1) : "";
    const std::string_view target = arguments.ArgC() >= 3 ? arguments.Arg(2) : "";
    const std::string_view value = arguments.ArgC() >= 4 ? arguments.Arg(3) : "";

    if (verb == "list")
        PrintLoaded();
    else if (verb == "status")
        PrintStatus(target);
    else if (verb == "load" && !target.empty())
        Queue(RequestKind::Load, target);
    else if (verb == "unload" && !target.empty())
        Queue(RequestKind::Unload, target);
    else if (verb == "reload" && !target.empty())
        Queue(RequestKind::Reload, target);
    else if (verb == "log" && !target.empty() && !value.empty())
        SetLogLevel(target, value);
    else
        Log::Info("Usage: {}", CommandUsage);
}

void PluginLoader::SetLogLevel(std::string_view name, std::string_view level)
{
    const std::optional<LogLevel> wanted = Parse<LogLevel>(level);
    if (!wanted)
    {
        Log::Warn("'{}' is not a log level. Use info, warn or error.", level);
        return;
    }

    PluginContext* context = _host.ContextFor(name);
    if (context == nullptr)
    {
        Log::Warn("'{}' is not loaded.", name);
        return;
    }

    context->SetMinLevel(*wanted);
    Log::Info("{} now logs {} and above.", name, Strings::ToLower(Name(*wanted)));
}

void PluginLoader::Queue(RequestKind kind, std::string_view name)
{
    _queue.push_back({.Kind = kind, .Name = std::string(name)});
    Log::Info("Queued {} of '{}' for the next frame.", Strings::ToLower(Name(kind)), name);
}

void PluginLoader::PrintLoaded() const
{
    if (_loaded.empty())
    {
        Log::Info("No plugins are loaded.");
        return;
    }

    Log::Info("{} plugin(s), in load order:", _loaded.size());
    for (const LoadedPlugin& plugin : _loaded)
        Log::Info("  {} v{}", plugin.Manifest.Name, plugin.Version);
}

void PluginLoader::PrintStatus(std::string_view name)
{
    if (!name.empty())
    {
        const LoadedPlugin* plugin = FindLoaded(name);
        if (plugin == nullptr)
        {
            Log::Warn("'{}' is not loaded.", name);
            return;
        }

        const char* status = plugin->Descriptor->Status();
        Log::Info("{}: {}", name, status != nullptr ? status : "{}");
        return;
    }

    for (const LoadedPlugin& plugin : _loaded)
    {
        const char* status = plugin.Descriptor->Status();
        Log::Info("{}: {}", plugin.Manifest.Name, status != nullptr ? status : "{}");
    }
}

void PluginLoader::RunLoad(std::string_view name)
{
    if (FindLoaded(name) != nullptr)
    {
        Log::Warn("'{}' is already loaded.", name);
        return;
    }

    const std::vector<InstalledPlugin> installed = Discover();
    const auto found =
        std::ranges::find_if(installed, [&](const InstalledPlugin& plugin) { return plugin.Manifest.Name == name; });
    if (found == installed.end())
    {
        Log::Warn("'{}' is not installed.", name);
        return;
    }

    for (const std::string& dependency : found->Manifest.Dependencies)
    {
        if (FindLoaded(dependency) == nullptr)
        {
            Log::Warn("Refusing to load '{}': it requires '{}', which is not loaded.", name, dependency);
            return;
        }
    }

    if (Status loaded = LoadOne(*found); !loaded)
        Log::Error("Refusing '{}': {}", name, loaded.error().Detail);
}

void PluginLoader::RunUnload(std::string_view name)
{
    if (FindLoaded(name) == nullptr)
    {
        Log::Warn("'{}' is not loaded.", name);
        return;
    }

    const std::vector<std::string> dependents = PluginOrder::RequiredDependents(name, LoadedManifests());
    if (!dependents.empty())
    {
        Log::Warn("Refusing to unload '{}': {} still requires it.", name, Strings::Join(dependents, ", "));
        return;
    }

    UnloadOne(name);
}

void PluginLoader::RunReload(std::string_view name)
{
    if (FindLoaded(name) == nullptr)
    {
        Log::Warn("'{}' is not loaded.", name);
        return;
    }

    // Whatever requires it goes down and comes back with it.
    std::vector<std::string> group = PluginOrder::RequiredDependents(name, LoadedManifests());
    group.emplace_back(name);
    auto inGroup = [&group](std::string_view plugin) { return std::ranges::find(group, plugin) != group.end(); };

    std::vector<std::string> going;
    for (auto plugin = _loaded.rbegin(); plugin != _loaded.rend(); ++plugin)
    {
        if (inGroup(plugin->Manifest.Name))
            going.push_back(plugin->Manifest.Name);
    }
    for (const std::string& plugin : going)
        UnloadOne(plugin);

    // Read the manifests again: a rebuilt plugin may declare different dependencies.
    const std::vector<InstalledPlugin> installed = Discover();

    std::vector<PluginManifest> manifests;
    manifests.reserve(installed.size());
    for (const InstalledPlugin& plugin : installed)
        manifests.push_back(plugin.Manifest);

    const LoadPlan plan = PluginOrder::Plan(manifests);
    for (const RefusedPlugin& refused : plan.Refused)
    {
        if (inGroup(refused.Name))
            Log::Error("Refusing '{}': {}", refused.Name, refused.Reason.Detail);
    }

    for (const std::string& next : plan.Load)
    {
        if (!inGroup(next) || FindLoaded(next) != nullptr)
            continue;

        const auto found = std::ranges::find_if(
            installed, [&](const InstalledPlugin& plugin) { return plugin.Manifest.Name == next; });
        if (Status loaded = LoadOne(*found); !loaded)
            Log::Error("Refusing '{}': {}", next, loaded.error().Detail);
    }
}

}  // namespace VoltMod
