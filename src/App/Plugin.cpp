#include "Schema/Layout.hpp"

#include <VoltMod/App/Internal/PluginModule.hpp>
#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <VoltMod/Engine/Detours.hpp>
#include <VoltMod/Host/IHostGameData.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>
#include <eiface.h>
#include <engine/igameeventsystem.h>
#include <exception>
#include <format>
#include <icvar.h>
#include <interfaces/interfaces.h>
#include <memory>
#include <networksystem/inetworkmessages.h>
#include <schemasystem/schemasystem.h>
#include <string>
#include <string_view>
#include <tier1/convar.h>

namespace VoltMod::Internal
{

/** The C function the host calls for @p Member. Nothing may unwind into the host. */
template <auto Member>
struct HostCallback;

template <class Result, class... Args, Result (PluginModule::*Member)(Args...)>
struct HostCallback<Member>
{
    static Result Call(void* module, Args... args) noexcept
    {
        try
        {
            return (static_cast<PluginModule*>(module)->*Member)(args...);
        }
        catch (const std::exception& error)
        {
            Log::Error("Unhandled exception in a host event: {}", error.what());
            return Result();
        }
        catch (...)
        {
            Log::Error("Unhandled non-standard exception in a host event.");
            return Result();
        }
    }
};

PluginModule::~PluginModule()
{
    Detach();
}

bool PluginModule::Attach(IHost& host, char* error, size_t errorSize) noexcept
{
    try
    {
        const bool loaded = AttachImpl(host, error, errorSize);
        if (!loaded)
        {
            _host = nullptr;
        }
        return loaded;
    }
    catch (const std::exception& exception)
    {
        Strings::CopyToBuffer(error, errorSize, exception.what());
        Shutdown();
        _host = nullptr;
        return false;
    }
    catch (...)
    {
        Strings::CopyToBuffer(error, errorSize, "plugin threw a non-standard exception during load");
        Shutdown();
        _host = nullptr;
        return false;
    }
}

using InterfaceLookup = void* (IHost::*)(const char* version) const;

template <class T>
static Status Resolve(T*& field, const IHost& host, InterfaceLookup lookup, const char* version)
{
    field = static_cast<T*>((host.*lookup)(version));
    if (!field)
    {
        return std::unexpected(Error::NotReady(std::format("Could not find interface: {}", version)));
    }
    return {};
}

/** Resolve the engine interfaces and bind the host's gamedata. Fails when an interface is missing; a
 *  gamedata failure is kept in `GameData`, and the services that need it say so. */
static Result<std::unique_ptr<UnsafeServices>> OpenUnsafe(IHost& host)
{
    auto unsafe = std::make_unique<UnsafeServices>();
    auto& gi = unsafe->Interfaces;

    constexpr InterfaceLookup server = &IHost::ServerInterface;
    constexpr InterfaceLookup engine = &IHost::EngineInterface;
    const Status resolved[] = {
        Resolve(gi.ServerGameDLL, host, server, INTERFACEVERSION_SERVERGAMEDLL),
        Resolve(gi.ServerGameClients, host, server, INTERFACEVERSION_SERVERGAMECLIENTS),
        Resolve(gi.NetworkServerService, host, engine, NETWORKSERVERSERVICE_INTERFACE_VERSION),
        Resolve(gi.GameEntities, host, server, INTERFACEVERSION_SERVERGAMEENTS),
        Resolve(gi.Engine, host, engine, INTERFACEVERSION_VENGINESERVER),
        Resolve(gi.GameEventSystem, host, engine, GAMEEVENTSYSTEM_INTERFACE_VERSION),
        Resolve(gi.NetworkMessages, host, engine, NETWORKMESSAGES_INTERFACE_VERSION),
        Resolve(gi.SchemaSystem, host, engine, SCHEMASYSTEM_INTERFACE_VERSION),
        Resolve(gi.CVar, host, engine, CVAR_INTERFACE_VERSION),
        Resolve(gi.GameResourceService, host, engine, GAMERESOURCESERVICESERVER_INTERFACE_VERSION),
    };
    for (const Status& status : resolved)
    {
        if (!status)
        {
            return std::unexpected(status.error());
        }
    }

    // Register pending tier1 ConCommands before the engine invokes ServerCommand instances.
    g_pCVar = gi.CVar;
    ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);

    // The host read and scanned gamedata once for the process; this only takes the numbers.
    if (IHostGameData* gameData = host.GameData())
    {
        unsafe->GameData = unsafe->Bindings.Bind(
            [gameData](GameDataSection sections, std::string_view name) { return gameData->Lookup(sections, name); });
    }
    else
    {
        unsafe->GameData = std::unexpected(Error::Engine("the host has no gamedata"));
    }
    return unsafe;
}

/** The host's one schema check covers this plugin only when both baked the same layout. */
static Status CheckSchema(const IHost& host)
{
    if (host.SchemaLayoutStamp() != Schema::GeneratedLayoutStamp())
    {
        return std::unexpected(Error::Invalid(
            std::format("this plugin was built against another schema layout (plugin {:016X}, host {:016X}); "
                        "rebuild it against this VoltMod",
                        Schema::GeneratedLayoutStamp(), host.SchemaLayoutStamp())));
    }
    if (!host.SchemaVerified())
    {
        return std::unexpected(Error::Invalid("the host found schema drift; its log names every field"));
    }
    return {};
}

bool PluginModule::AttachImpl(IHost& host, char* error, size_t errorSize)
{
    KHook::__exported__khook = host.HookDispatcher();
    _host = &host;

    // The host owns the console and prefixes this plugin's log tag; services log while they are built.
    Log::SetMinimumLevel(static_cast<LogLevel>(host.MinLogLevel()));
    Log::SetHandler(
        [&host](LogLevel level, std::string_view message) { host.WriteLog(static_cast<uint8_t>(level), message); });
    SetBaseDir(host.BaseDir());

    if (Status schema = CheckSchema(host); !schema)
    {
        return Refuse(std::format("SchemaLayout: {}", schema.error().Detail), error, errorSize);
    }

    auto unsafe = OpenUnsafe(host);
    if (!unsafe)
    {
        return Refuse(unsafe.error().Detail, error, errorSize);
    }
    _unsafe = std::move(*unsafe);
    _runtime = std::make_unique<Runtime>(host, *_unsafe);

    // Members that load settings record a required step, so a failure refuses the plugin before Load.
    _plugin = _factory(*_runtime);
    if (std::string reason = _runtime->LoadSteps.AbortReason(); !reason.empty())
    {
        return Refuse(reason, error, errorSize);
    }

    SubscribeHostEvents();

    if (!_plugin->Load())
    {
        std::string reason = _runtime->LoadSteps.AbortReason();
        return Refuse(reason.empty() ? "Load returned false" : reason, error, errorSize);
    }

    Log::Info("{}", _runtime->LoadSteps.Summary());
    return true;
}

bool PluginModule::Refuse(std::string_view reason, char* error, size_t errorSize)
{
    if (_runtime && _runtime->LoadSteps.Count() > 0)
    {
        Log::Info("{}", _runtime->LoadSteps.Summary());
    }
    Strings::CopyToBuffer(error, errorSize, reason);
    Shutdown();
    return false;
}

void PluginModule::Detach() noexcept
{
    Shutdown();
    _host = nullptr;
}

// Stop commands before destroying their captured state, then drop host events and services.
void PluginModule::Shutdown() noexcept
{
    if (_runtime)
    {
        _runtime->Commands.RemoveAll();
    }
    _plugin.reset();
    _hostEvents.Clear();
    _runtime.reset();
    _unsafe.reset();
}

const char* PluginModule::StatusJson() noexcept
{
    try
    {
        _status = _runtime ? _runtime->Status.BuildJson() : std::string("{}");
        return _status.c_str();
    }
    catch (...)
    {
        return "{}";
    }
}

void PluginModule::SubscribeHostEvents()
{
    IHostEvents& events = _host->Events();
    auto keep = [&](uint64_t token) { _hostEvents.Add(Subscription([&events, token] { events.Unsubscribe(token); })); };

    keep(events.OnFrame(&HostCallback<&PluginModule::OnFrame>::Call, this));
    keep(events.OnServerStartup(&HostCallback<&PluginModule::OnServerStartup>::Call, this));
    keep(events.OnClientConnecting(&HostCallback<&PluginModule::OnClientConnecting>::Call, this));
    keep(events.OnClientConnected(&HostCallback<&PluginModule::OnClientConnected>::Call, this));
    keep(events.OnClientDisconnected(&HostCallback<&PluginModule::OnClientDisconnected>::Call, this));
    keep(events.OnClientFullyConnected(&HostCallback<&PluginModule::OnClientFullyConnected>::Call, this));
    keep(events.OnClientSettingsChanged(&HostCallback<&PluginModule::OnClientSettingsChanged>::Call, this));
    keep(events.OnConsoleCommand(&HostCallback<&PluginModule::OnConsoleCommand>::Call, this));
    keep(events.OnCheckTransmit(&HostCallback<&PluginModule::OnCheckTransmit>::Call, this));
    keep(events.OnBuildGameSessionManifest(&HostCallback<&PluginModule::OnBuildGameSessionManifest>::Call, this));
}

void PluginModule::OnFrame()
{
    // `volt log` changes this; reading it once a frame lets the log helpers skip silenced lines.
    Log::SetMinimumLevel(static_cast<LogLevel>(_host->MinLogLevel()));
    _runtime->OnGameFrame();
}

void PluginModule::OnServerStartup(std::string_view mapName)
{
    Log::Info("Server startup: map '{}'.", mapName.empty() ? std::string_view("<none>") : mapName);
    _runtime->Map.SetCurrent(std::string(mapName));
    _runtime->Entities.OnServerStartup();
    _runtime->GameEvents.OnServerStartup();
    _runtime->Hooks.ClientConVars.OnServerStartup();
    _runtime->Map.Started.Raise(mapName);
}

bool PluginModule::OnClientConnecting(int slot, int64_t steamId, std::string_view name, char* reason, size_t reasonSize)
{
    ConnectRequest request{.Slot = slot, .SteamId = steamId, .Name = name};
    _runtime->Players.Connecting.Raise(request);
    if (request.Rejected)
    {
        Strings::CopyToBuffer(reason, reasonSize, request.Reason);
    }
    return request.Rejected;
}

void PluginModule::OnClientConnected(int slot, int64_t steamId, std::string_view name, std::string_view address)
{
    _runtime->Players.Add(slot, steamId, std::string(name), std::string(address));
}

void PluginModule::OnClientDisconnected(int slot)
{
    _runtime->Players.Remove(slot);
}

void PluginModule::OnClientFullyConnected(int slot)
{
    _runtime->Hooks.ClientConVars.OnClientFullyConnect(slot);
    _runtime->Players.OnClientFullyConnected(slot);
}

void PluginModule::OnClientSettingsChanged(int slot)
{
    _runtime->Players.OnClientSettingsChanged(slot);
}

void PluginModule::OnCheckTransmit(CCheckTransmitInfo** infoList, int infoCount)
{
    _runtime->Hooks.Visibility.OnCheckTransmit(infoList, infoCount);
}

void PluginModule::OnBuildGameSessionManifest(IEntityResourceManifest* manifest)
{
    _runtime->Precache.AddTo(*manifest);
}

}  // namespace VoltMod::Internal
