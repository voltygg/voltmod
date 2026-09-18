#include "Host/Loading/VoltCommand.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/EnumNames.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <optional>
#include <tier1/convar.h>
#include <vector>

namespace VoltMod
{

static constexpr std::string_view CommandUsage =
    "volt list | status [name] | load <name> | unload <name> | reload <name> | "
    "log <name> <info|warn|error>";

VoltCommand::VoltCommand(PluginHost& host, PluginLoader& loader) : _host(host), _loader(loader)
{
    _host.RegisterHostCommand("volt");
    _command =
        std::make_unique<ServerCommand>("volt", CommandUsage, [this](const CCommand& arguments) { Run(arguments); });
}

VoltCommand::~VoltCommand() = default;

void VoltCommand::Run(const CCommand& arguments)
{
    const std::string_view verb = arguments.ArgC() >= 2 ? arguments.Arg(1) : "";
    const std::string_view target = arguments.ArgC() >= 3 ? arguments.Arg(2) : "";
    const std::string_view value = arguments.ArgC() >= 4 ? arguments.Arg(3) : "";

    if (verb == "list")
        PrintLoaded();
    else if (verb == "status")
        PrintStatus(target);
    else if (verb == "load" && !target.empty())
        _loader.Defer(PluginLoader::ActionKind::Load, target);
    else if (verb == "unload" && !target.empty())
        _loader.Defer(PluginLoader::ActionKind::Unload, target);
    else if (verb == "reload" && !target.empty())
        _loader.Defer(PluginLoader::ActionKind::Reload, target);
    else if (verb == "log" && !target.empty() && !value.empty())
        SetLogLevel(target, value);
    else
        Log::Info("Usage: {}", CommandUsage);
}

void VoltCommand::PrintLoaded() const
{
    const std::vector<LoadedPlugin>& loaded = _loader.Loaded();
    if (loaded.empty())
    {
        Log::Info("No plugins are loaded.");
        return;
    }

    Log::Info("{} plugin(s), in load order:", loaded.size());
    for (const LoadedPlugin& plugin : loaded)
    {
        const std::string_view description = plugin.Manifest.Description;
        Log::Info("  {} v{}{}{}", plugin.Manifest.Name, plugin.Version(), description.empty() ? "" : " - ",
                  description);
    }
}

void VoltCommand::PrintStatus(std::string_view name)
{
    const auto print = [](const LoadedPlugin& plugin) {
        const char* status = plugin.Descriptor->Status();
        Log::Info("{}: {}", plugin.Manifest.Name, status != nullptr ? status : "{}");
    };

    if (name.empty())
    {
        for (const LoadedPlugin& plugin : _loader.Loaded())
            print(plugin);
        return;
    }

    if (const LoadedPlugin* plugin = _loader.RequireLoaded(name); plugin != nullptr)
        print(*plugin);
}

void VoltCommand::SetLogLevel(std::string_view name, std::string_view level)
{
    const std::optional<LogLevel> wanted = Parse<LogLevel>(level);
    if (!wanted)
    {
        Log::Warn("'{}' is not a log level. Use info, warn or error.", level);
        return;
    }

    HostView* plugin = _loader.RequireLoaded(name) != nullptr ? _host.FindPlugin(name) : nullptr;
    if (plugin == nullptr)
        return;

    plugin->SetMinLogLevel(*wanted);
    Log::Info("{} now logs {} and above.", name, Strings::ToLower(Name(*wanted)));
}

}  // namespace VoltMod
