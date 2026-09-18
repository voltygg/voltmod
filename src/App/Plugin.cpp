#include <VoltMod/App/Plugin.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <VoltMod/Engine/Detours.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>
#include <cstdio>
#include <exception>
#include <format>
#include <string>
#include <vector>

namespace VoltMod
{

/** The C function the host calls for @p Member. Nothing may unwind into the host: the two modules
 *  do not share a runtime. */
template <auto Member>
struct HostCallback;

template <class Result, class... Args, Result (Plugin::*Member)(Args...)>
struct HostCallback<Member>
{
    static Result Call(void* plugin, Args... args) noexcept
    {
        try
        {
            return (static_cast<Plugin*>(plugin)->*Member)(args...);
        }
        catch (const std::exception& error)
        {
            Log::Error("Unhandled exception in a host event: {}", error.what());
            return Result();
        }
    }
};

Plugin::Plugin() = default;
Plugin::~Plugin() = default;

bool Plugin::Attach(IHost& host, const PluginBuild& build, char* error, size_t errorSize)
{
    // KHook resolves its entry points against this module's own pointer, so seed it before any hook goes up.
    KHook::__exported__khook = host.HookDispatcher();
    _host = &host;

    _runtime = std::make_unique<Runtime>();
    // Attach before Start so a load step can already reach a peer's published interface.
    _runtime->Exchange.Attach(&host.Services());
    _runtime->Commands.Attach(&host);

    const LoadContext context{.Host = &host, .Version = build.Version, .Error = error, .MaxLen = errorSize};
    if (!_runtime->Start(context))
    {
        if (_runtime->LoadSteps.Count() > 0)
            Log::Info("{}", _runtime->LoadSteps.Summary());
        _runtime.reset();
        return false;
    }

    _runtime->Status.RegisterSection("build", [name = _runtime->PluginName, build] {
        return Json::Write(glz::obj{"name", name, "version", std::string_view(build.Version), "commit",
                                    std::string_view(build.Commit), "date", std::string_view(build.Date)});
    });

    SubscribeHostEvents();
    OnRegisterHooks(*_runtime, _customHooks);

    if (!OnLoad(*_runtime))
    {
        std::string failure = _runtime->LoadSteps.AbortReason();
        if (failure.empty())
            failure = "OnLoad returned false";
        Log::Info("{}", _runtime->LoadSteps.Summary());
        snprintf(error, errorSize, "%s", failure.c_str());
        Shutdown();
        return false;
    }

    _runtime->LoadSteps.Optional("Permissions", [this]() -> Status {
        const std::vector<std::string> missing = _runtime->Commands.CommandsMissingPolicy();
        if (missing.empty())
            return {};
        return std::unexpected(
            Error::Invalid(std::format("{} command(s) gate on a permission with no HasPermission policy "
                                       "installed and will be denied ({}); set Runtime::Policy.HasPermission "
                                       "in OnLoad",
                                       missing.size(), Strings::Join(missing, ", "))));
    });

    Log::Info("{}", _runtime->LoadSteps.Summary());
    return true;
}

void Plugin::Detach()
{
    Shutdown();
    _host = nullptr;
}

// Stop callbacks into plugin state before OnUnload, then drop the host events and the services.
void Plugin::Shutdown()
{
    _customHooks.Clear();
    if (_runtime)
        _runtime->Commands.RemoveAll();
    OnUnload();
    _hostEvents.Clear();
    _runtime.reset();
}

const char* Plugin::StatusJson()
{
    _status = _runtime ? _runtime->Status.BuildJson() : std::string("{}");
    return _status.c_str();
}

void Plugin::SubscribeHostEvents()
{
    IHostEvents& events = _host->Events();
    auto keep = [&](uint64_t token) {
        _hostEvents.Add(Subscription([&events, token] { events.Unsubscribe(token); }));
    };

    keep(events.OnFrame(&HostCallback<&Plugin::HostFrame>::Call, this));
    keep(events.OnServerStartup(&HostCallback<&Plugin::HandleServerStartup>::Call, this));
    keep(events.OnClientConnected(&HostCallback<&Plugin::HostClientConnected>::Call, this));
    keep(events.OnClientDisconnected(&HostCallback<&Plugin::HostClientDisconnected>::Call, this));
    keep(events.OnClientFullyConnected(&HostCallback<&Plugin::HostClientFullyConnected>::Call, this));
    keep(events.OnClientSettingsChanged(&HostCallback<&Plugin::HostClientSettingsChanged>::Call, this));
    keep(events.OnConsoleCommand(&HostCallback<&Plugin::HandleConsoleCommand>::Call, this));
    keep(events.OnCheckTransmit(&HostCallback<&Plugin::HostCheckTransmit>::Call, this));
}

void Plugin::HostFrame()
{
    // `volt log` changes this; reading it once a frame lets the log helpers skip formatting silenced lines.
    Log::SetMinimumLevel(static_cast<LogLevel>(_host->MinLogLevel()));
    _runtime->OnGameFrame();
}

void Plugin::HandleServerStartup(std::string_view mapName)
{
    Log::Info("Server startup: map '{}'.", mapName.empty() ? std::string_view("<none>") : mapName);
    _runtime->Map.SetCurrent(std::string(mapName));
    // Publish the new entity system before the plugin callback.
    _runtime->Entities.OnServerStartup();
    _runtime->GameEvents.OnServerStartup();
    _runtime->Hooks.ClientConVars.OnServerStartup();
    OnServerStartup(mapName);
}

void Plugin::HostClientConnected(int slot, int64_t steamId, std::string_view name, std::string_view address)
{
    _runtime->Players.Add(slot, steamId, std::string(name), std::string(address));
}

void Plugin::HostClientDisconnected(int slot)
{
    _runtime->Players.Remove(slot);
}

void Plugin::HostClientFullyConnected(int slot)
{
    _runtime->Hooks.ClientConVars.OnClientFullyConnect(slot);
    _runtime->Players.OnClientFullyConnected(slot);
}

void Plugin::HostClientSettingsChanged(int slot)
{
    _runtime->Players.OnClientSettingsChanged(slot);
}

void Plugin::HostCheckTransmit(CCheckTransmitInfo** infoList, int infoCount)
{
    _runtime->Hooks.Visibility.OnCheckTransmit(infoList, infoCount);
}

}  // namespace VoltMod
