#include "Host/Loading/PluginLoader.hpp"

#include "Host/Loading/PluginDependencies.hpp"

#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/EnumNames.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <algorithm>
#include <format>
#include <ranges>
#include <utility>

namespace VoltMod
{

#if defined(_WIN32)
static constexpr std::string_view LibrarySuffix = ".dll";
#else
static constexpr std::string_view LibrarySuffix = ".so";
#endif

// Whatever a plugin still held when its view closed is a bug in that plugin; the host has already
// dropped it by the time this runs.
static void WarnUnreleased(std::string_view name, const Unreleased& unreleased)
{
    for (std::string_view event : unreleased.Subscriptions)
    {
        Log::Warn("{} left a {} subscription behind; the host dropped it.", name, event);
    }
    for (const std::string& service : unreleased.Services)
    {
        Log::Warn("{} left the service '{}' published; the host withdrew it.", name, service);
    }
}

static std::filesystem::path LibraryPath(std::string_view name)
{
    return ResolvePath(PluginFile(name, std::format("{}{}", name, LibrarySuffix)));
}

static Discovered Installed()
{
    return InstalledPlugins::Discover(ResolvePath("addons/voltmod/plugins"));
}

PluginLoader::PluginLoader(PluginHost& host) : _host(host) {}

PluginLoader::~PluginLoader()
{
    UnloadAll();
}

void PluginLoader::LoadAll()
{
    const Discovered installed = Installed();
    LoadGroup(installed, [](std::string_view) { return true; });
    Log::Info("{} of {} installed plugin(s) loaded.", _loaded.size(),
              installed.Plugins.size() + installed.Refused.size());
}

void PluginLoader::UnloadAll()
{
    _pending.clear();
    while (!_loaded.empty())
    {
        UnloadOne(_loaded.back().Manifest.Name);
    }
}

void PluginLoader::Defer(ActionKind kind, std::string_view name)
{
    _pending.push_back({.Kind = kind, .Name = std::string(name)});
    Log::Info("Queued {} of '{}' for the next frame.", Strings::ToLower(Name(kind)), name);
}

void PluginLoader::RunPending()
{
    if (_pending.empty())
    {
        return;
    }

    // Take the list aside: an action that defers another leaves it for the next frame.
    const std::vector<PendingAction> actions = std::move(_pending);
    _pending.clear();

    for (const PendingAction& action : actions)
    {
        switch (action.Kind)
        {
        case ActionKind::Load:
            RunLoad(action.Name);
            break;
        case ActionKind::Unload:
            RunUnload(action.Name);
            break;
        case ActionKind::Reload:
            RunReload(action.Name);
            break;
        }
    }
}

void PluginLoader::LoadGroup(const Discovered& installed, const std::function<bool(std::string_view)>& wanted)
{
    const LoadList list = PluginDependencies::Resolve(installed.Plugins);
    for (const std::vector<RefusedPlugin>* refusals : {&installed.Refused, &list.Refused})
    {
        for (const RefusedPlugin& refused : *refusals)
        {
            if (wanted(refused.Name))
            {
                Refuse(refused.Name, refused.Reason.Detail);
            }
        }
    }

    for (const std::string& name : list.Allowed)
    {
        if (!wanted(name) || FindLoaded(name) != nullptr)
        {
            continue;
        }

        const auto found = std::ranges::find(installed.Plugins, name, &PluginManifest::Name);
        if (Status loaded = LoadOne(*found); !loaded)
        {
            Refuse(name, loaded.error().Detail);
        }
    }
}

void PluginLoader::Refuse(std::string_view name, std::string reason)
{
    Log::Error("Refusing '{}': {}", name, reason);
    _refused.insert_or_assign(std::string(name), std::move(reason));
}

Status PluginLoader::LoadOne(const PluginManifest& manifest)
{
    const std::string& name = manifest.Name;

    Result<SharedLibrary> code = SharedLibrary::Open(LibraryPath(name));
    if (!code)
    {
        return std::unexpected(code.error());
    }

    const Result<void*> entry = code->Symbol(PluginEntryName);
    if (!entry)
    {
        return std::unexpected(entry.error());
    }

    using EntryPoint = const PluginDescriptor* (*)();
    const PluginDescriptor* descriptor = reinterpret_cast<EntryPoint>(*entry)();
    if (Status valid = ValidateDescriptor(descriptor); !valid)
    {
        return valid;
    }

    HostView* view = _host.AddPlugin(name, manifest.LogTag, manifest.Version);
    if (view == nullptr)
    {
        return std::unexpected(Error::Failed("the host already holds a view under that name"));
    }
    view->SetMinLogLevel(manifest.LogLevel);

    char failure[512] = {};
    if (!descriptor->Load(view, failure, sizeof failure))
    {
        failure[sizeof failure - 1] = '\0';
        // A plugin that refuses its own load has already torn itself down, so the host only lets
        // go of what it took; the library is freed as `code` leaves this scope.
        WarnUnreleased(name, _host.RemovePlugin(name));
        return std::unexpected(Error::Failed(failure[0] != '\0' ? failure : "its Load returned false"));
    }

    _loaded.push_back({.Manifest = manifest, .Descriptor = descriptor, .Code = std::move(*code)});
    _refused.erase(name);

    Log::Info("Loaded {} v{}.", name, manifest.Version);
    return {};
}

void PluginLoader::UnloadOne(std::string_view name)
{
    // Copy it: the view may point into the record this is about to erase.
    const std::string plugin(name);

    const auto found =
        std::ranges::find_if(_loaded, [&](const LoadedPlugin& loaded) { return loaded.Manifest.Name == plugin; });
    if (found == _loaded.end())
    {
        return;
    }

    found->Descriptor->Unload();
    WarnUnreleased(plugin, _host.RemovePlugin(plugin));

    // Only now is the library freed: every hook thunk and subscription closure it installed is
    // code inside it, and Unload is what takes them down.
    _loaded.erase(found);
    Log::Info("Unloaded {}.", plugin);
}

LoadedPlugin* PluginLoader::FindLoaded(std::string_view name)
{
    const auto found =
        std::ranges::find_if(_loaded, [name](const LoadedPlugin& plugin) { return plugin.Manifest.Name == name; });
    return found != _loaded.end() ? &*found : nullptr;
}

LoadedPlugin* PluginLoader::RequireLoaded(std::string_view name)
{
    LoadedPlugin* plugin = FindLoaded(name);
    if (plugin == nullptr)
    {
        Log::Warn("'{}' is not loaded.", name);
    }
    return plugin;
}

std::vector<PluginManifest> PluginLoader::LoadedManifests() const
{
    std::vector<PluginManifest> manifests;
    manifests.reserve(_loaded.size());
    for (const LoadedPlugin& plugin : _loaded)
    {
        manifests.push_back(plugin.Manifest);
    }
    return manifests;
}

void PluginLoader::RunLoad(std::string_view name)
{
    if (FindLoaded(name) != nullptr)
    {
        Log::Warn("'{}' is already loaded.", name);
        return;
    }

    const Discovered installed = Installed();
    if (const auto broken = std::ranges::find(installed.Refused, name, &RefusedPlugin::Name);
        broken != installed.Refused.end())
    {
        Refuse(name, broken->Reason.Detail);
        return;
    }

    const auto found = std::ranges::find(installed.Plugins, name, &PluginManifest::Name);
    if (found == installed.Plugins.end())
    {
        Log::Warn("'{}' is not installed.", name);
        return;
    }

    for (const std::string& dependency : found->Dependencies)
    {
        if (FindLoaded(dependency) == nullptr)
        {
            Refuse(name, std::format("it requires '{}', which is not loaded.", dependency));
            return;
        }
    }

    if (Status loaded = LoadOne(*found); !loaded)
    {
        Refuse(name, loaded.error().Detail);
    }
}

void PluginLoader::RunUnload(std::string_view name)
{
    if (RequireLoaded(name) == nullptr)
    {
        return;
    }

    const std::vector<std::string> dependents = PluginDependencies::RequiredDependents(name, LoadedManifests());
    if (!dependents.empty())
    {
        Log::Warn("Refusing to unload '{}': {} still requires it.", name, Strings::Join(dependents, ", "));
        return;
    }

    UnloadOne(name);
}

void PluginLoader::RunReload(std::string_view name)
{
    if (RequireLoaded(name) == nullptr)
    {
        return;
    }

    // Whatever requires it goes down and comes back with it, each one before what it requires.
    std::vector<std::string> group = PluginDependencies::RequiredDependents(name, LoadedManifests());
    group.emplace_back(name);

    std::vector<std::string> going;
    for (const LoadedPlugin& plugin : _loaded | std::views::reverse)
    {
        if (std::ranges::find(group, plugin.Manifest.Name) != group.end())
        {
            going.push_back(plugin.Manifest.Name);
        }
    }

    for (const std::string& plugin : going)
    {
        UnloadOne(plugin);
    }

    // Read the manifests again: a rebuilt plugin may declare different dependencies.
    LoadGroup(Installed(),
              [&group](std::string_view plugin) { return std::ranges::find(group, plugin) != group.end(); });
}

}  // namespace VoltMod
