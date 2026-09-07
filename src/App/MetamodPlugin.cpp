#include <VoltMod/App/MetamodPlugin.hpp>
#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Unsafe/HookMacros.hpp>
#include <cstdio>
#include <cstring>
#include <format>
#include <iserver.h>
#include <string>
#include <string_view>
#include <vector>

// PLUGIN_GLOBALVARS is defined by each plugin's VOLTMOD_PLUGIN.

// SourceHook needs a complete type for this by-reference hook parameter.
class GameSessionConfiguration_t
{};

namespace VoltMod
{

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&,
                   ISource2WorldSession*, const char*);
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char*, uint64, const char*,
                   const char*, bool);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, ENetworkDisconnectionReason,
                   const char*, uint64, const char*);
SH_DECL_HOOK1_void(IServerGameClients, ClientFullyConnect, SH_NOATTRIB, 0, CPlayerSlot);
SH_DECL_HOOK1_void(IServerGameClients, ClientSettingsChanged, SH_NOATTRIB, 0, CPlayerSlot);
SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandRef, const CCommandContext&, const CCommand&);
SH_DECL_HOOK7_void(ISource2GameEntities, CheckTransmit, SH_NOATTRIB, 0, CCheckTransmitInfo**, int, CBitVec<16384>&,
                   CBitVec<16384>&, const Entity2Networkable_t**, const uint16*, int);

MetamodPlugin::MetamodPlugin() = default;
MetamodPlugin::~MetamodPlugin() = default;

bool MetamodPlugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();
    _info = Info();

    _runtime = std::make_unique<Runtime>();

    const LoadContext context{.Ismm = ismm, .Error = error, .MaxLen = maxlen, .LogPrefix = _info.LogTag};
    if (!_runtime->Start(context))
    {
        if (!_runtime->LoadReport.Stages().empty())
            Log::Info("{}", _runtime->LoadReport.Summary());
        _runtime.reset();
        return false;
    }

    _runtime->Status.RegisterSection("build", [info = _info] {
        return Json::Write(
            glz::obj{"name", info.Name, "version", info.Version, "commit", info.Commit, "date", info.Date});
    });

    RegisterStandardHooks();
    OnRegisterHooks(*_runtime);

    if (!OnLoad(*_runtime))
    {
        // Give a bare false return a useful load-report entry.
        if (_runtime->LoadReport.FirstFailure().empty())
            _runtime->LoadReport.Run("OnLoad", [] { return StageResult::Failed("OnLoad returned false"); });
        Log::Info("{}", _runtime->LoadReport.Summary());
        const std::string failure = _runtime->LoadReport.FirstFailure();
        snprintf(error, maxlen, "%s", failure.c_str());
        Shutdown();
        return false;
    }

    // Report permission-gated commands with no policy before the first invocation.
    _runtime->LoadReport.Run("Commands", [this] {
        const std::vector<std::string> missing = _runtime->Commands.CommandsMissingPolicy();
        if (missing.empty())
            return StageResult::Ok();
        return StageResult::Degraded(
            std::format("{} command(s) gate on a permission with no HasPermission policy "
                        "installed and will be denied ({}); set Runtime::Policy.HasPermission "
                        "in OnLoad",
                        missing.size(), Strings::Join(missing, ", ")));
    });

    Log::Info("{}", _runtime->LoadReport.Summary());
    if (_info.Commit != nullptr && *_info.Commit != '\0')
        Log::Info("Loaded {} v{} ({}, committed {}){}.", _info.Name, _info.Version, _info.Commit, _info.Date,
                  late ? " (late)" : "");
    else
        Log::Info("Loaded successfully{}.", late ? " (late)" : "");
    return true;
}

// Remove custom hooks and commands before plugin state, then standard hooks and the runtime.
void MetamodPlugin::Shutdown()
{
    _customHooks.Clear();
    if (_runtime)
        _runtime->Commands.RemoveAll();
    OnUnload();
    _standardHooks.Clear();
    _runtime.reset();
}

bool MetamodPlugin::Unload(char* error, size_t maxlen)
{
    Shutdown();
    return true;
}

bool MetamodPlugin::OnPlayerChat(Player* player, std::string_view message, bool /*teamChat*/)
{
    // Menu input consumes the line before command parsing.
    if (_runtime->Hooks.ChatInput.TryConsume(player->Slot(), message))
        return true;

    return _runtime->Commands.HandleChatMessage(player, message);
}

void MetamodPlugin::RegisterStandardHooks()
{
    auto& gi = _runtime->Unsafe.Interfaces;

    // Each scoped hook owns its removal subscription; clear them before the runtime.
    _standardHooks.Add(VOLTMOD_SCOPED_HOOK(IServerGameDLL, GameFrame, gi.ServerGameDLL,
                                           SH_MEMBER(this, &MetamodPlugin::Hook_GameFrame), true));
    _standardHooks.Add(VOLTMOD_SCOPED_HOOK(INetworkServerService, StartupServer, gi.NetworkServerService,
                                           SH_MEMBER(this, &MetamodPlugin::Hook_StartupServer), true));
    _standardHooks.Add(VOLTMOD_SCOPED_HOOK(IServerGameClients, OnClientConnected, gi.ServerGameClients,
                                           SH_MEMBER(this, &MetamodPlugin::Hook_OnClientConnected), false));
    _standardHooks.Add(VOLTMOD_SCOPED_HOOK(IServerGameClients, ClientDisconnect, gi.ServerGameClients,
                                           SH_MEMBER(this, &MetamodPlugin::Hook_ClientDisconnect), true));
    _standardHooks.Add(VOLTMOD_SCOPED_HOOK(IServerGameClients, ClientFullyConnect, gi.ServerGameClients,
                                           SH_MEMBER(this, &MetamodPlugin::Hook_ClientFullyConnect), true));
    _standardHooks.Add(VOLTMOD_SCOPED_HOOK(IServerGameClients, ClientSettingsChanged, gi.ServerGameClients,
                                           SH_MEMBER(this, &MetamodPlugin::Hook_ClientSettingsChanged), true));
    _standardHooks.Add(VOLTMOD_SCOPED_HOOK(ICvar, DispatchConCommand, gi.CVar,
                                           SH_MEMBER(this, &MetamodPlugin::Hook_DispatchConCommand), false));
    // The post hook filters the bit vectors after the game fills them.
    _standardHooks.Add(VOLTMOD_SCOPED_HOOK(ISource2GameEntities, CheckTransmit, gi.GameEntities,
                                           SH_MEMBER(this, &MetamodPlugin::Hook_CheckTransmit), true));

    Log::Info("Hooks registered.");
}

void MetamodPlugin::Hook_GameFrame(bool simulating, bool firstTick, bool lastTick)
{
    _runtime->OnGameFrame();
}

void MetamodPlugin::Hook_StartupServer(const GameSessionConfiguration_t&, ISource2WorldSession*, const char* mapName)
{
    Log::Info("Server startup: map '{}'.", mapName ? mapName : "<none>");
    _runtime->Map.SetCurrent(mapName ? mapName : "");
    // Publish the new entity system before calling the plugin callback.
    _runtime->Entities.OnServerStartup();
    _runtime->GameEvents.OnServerStartup();
    _runtime->Hooks.Teleport.OnServerStartup();
    _runtime->Hooks.ClientCvars.OnServerStartup();
    OnServerStartup(mapName ? std::string_view(mapName) : std::string_view{});
}

void MetamodPlugin::Hook_CheckTransmit(CCheckTransmitInfo** infoList, int infoCount, CBitVec<16384>&, CBitVec<16384>&,
                                       const Entity2Networkable_t**, const uint16*, int)
{
    _runtime->Hooks.Transmit.OnCheckTransmit(infoList, infoCount);
}

void MetamodPlugin::Hook_OnClientConnected(CPlayerSlot slot, const char* name, uint64 xuid, const char* networkId,
                                           const char* address, bool fakePlayer)
{
    // At this callback `name` is only a fallback; `address` is available here first.
    _runtime->Players.Add(slot.Get(), static_cast<int64_t>(xuid), name ? name : "", address ? address : "");
}

void MetamodPlugin::Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* name,
                                          uint64 xuid, const char* networkId)
{
    // Remove raises Disconnected before the slot is reused.
    _runtime->Players.Remove(slot.Get());
}

void MetamodPlugin::Hook_ClientFullyConnect(CPlayerSlot slot)
{
    _runtime->Hooks.ClientCvars.OnClientFullyConnect(slot.Get());
    _runtime->Players.OnClientFullyConnected(slot.Get());
}

void MetamodPlugin::Hook_ClientSettingsChanged(CPlayerSlot slot)
{
    _runtime->Players.OnClientSettingsChanged(slot.Get());
}

void MetamodPlugin::Hook_DispatchConCommand(ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args)
{
    const char* cmdName = cmd.GetName();
    if (!cmdName)
        return;

    bool isSay = (strcmp(cmdName, "say") == 0);
    bool isSayTeam = (strcmp(cmdName, "say_team") == 0);
    if (!isSay && !isSayTeam)
        return;

    if (args.ArgC() < 2)
        return;

    std::string_view message = args.Arg(1);
    if (message.size() >= 2 && message.front() == '"' && message.back() == '"')
    {
        message.remove_prefix(1);
        message.remove_suffix(1);
    }
    if (message.empty())
        return;

    int slotIdx = ctx.GetPlayerSlot().Get();
    if (!IsValidSlot(slotIdx))
        return;

    Player* player = _runtime->Players.Get(slotIdx);
    if (!player)
        return;

    if (OnPlayerChat(player, message, isSayTeam))
        RETURN_META(MRES_SUPERCEDE);
}

const char* MetamodPlugin::GetAuthor()
{
    return _info.Author;
}
const char* MetamodPlugin::GetName()
{
    return _info.Name;
}
const char* MetamodPlugin::GetDescription()
{
    return _info.Description;
}
const char* MetamodPlugin::GetURL()
{
    return _info.Url;
}
const char* MetamodPlugin::GetLicense()
{
    return _info.License;
}
const char* MetamodPlugin::GetVersion()
{
    return _info.Version;
}
const char* MetamodPlugin::GetDate()
{
    return _info.Date;
}
const char* MetamodPlugin::GetLogTag()
{
    return _info.LogTag;
}

void* MetamodPlugin::OnMetamodQuery(const char* iface, int* ret)
{
    // Unknown interfaces are normal; after unload the exchange is empty.
    void* impl = _runtime ? _runtime->Exchange.Find(iface) : nullptr;

    if (ret)
    {
        *ret = impl ? META_IFACE_OK : META_IFACE_FAILED;
    }

    return impl;
}

}  // namespace VoltMod
