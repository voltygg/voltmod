#include <CS2Kit/App/MetamodPlugin.hpp>
#include <CS2Kit/Core/HookMacros.hpp>
#include <CS2Kit/Core/Log.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Players/Player.hpp>
#include <CS2Kit/Players/PlayerManager.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <cstdio>
#include <cstring>
#include <format>
#include <iserver.h>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

// PLUGIN_GLOBALVARS ships in MetamodPlugin.hpp; the definitions come from
// each plugin's CS2KIT_PLUGIN.

// The SDK only forward-declares this (iloopmode.h keeps the real one commented out). SourceHook's
// param table needs a complete type; the hook receives it by reference and never looks inside.
class GameSessionConfiguration_t
{};

namespace CS2Kit::App
{

using namespace CS2Kit::Players;
using namespace CS2Kit::Sdk;
using namespace CS2Kit::Core;

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
    _lateLoad = late;
    _info = Info();  // capture once; the ISmmPlugin getters read this copy

    // Fresh runtime per load; wire the ambient pointer before Start so kit subsystems
    // (and the plugin's OnLoad) can reach it. Destroyed in Unload - this is what makes
    // meta reload start from clean state.
    _runtime = std::make_unique<Runtime>();
    Detail::SetRt(_runtime.get());

    const LoadContext context{.Ismm = ismm, .Error = error, .MaxLen = maxlen, .Late = late, .LogPrefix = _info.LogTag};
    if (!_runtime->Start(context))
    {
        if (!_runtime->LoadReport.Stages().empty())
            Log::Info("{}", _runtime->LoadReport.Summary());
        Detail::SetRt(nullptr);
        _runtime.reset();
        return false;
    }

    _runtime->Status.RegisterSection("build", [info = _info] {
        return nlohmann::json{
            {"name", info.Name}, {"version", info.Version}, {"commit", info.Commit}, {"date", info.Date}};
    });

    RegisterStandardHooks();
    OnRegisterHooks(*_runtime);

    if (!OnLoad(*_runtime, late))
    {
        // A bare `return false` still gets a named failure in the report and error buffer.
        if (_runtime->LoadReport.FirstFailure().empty())
            _runtime->LoadReport.Run("OnLoad", [] { return Core::StageResult::Failed("OnLoad returned false"); });
        Log::Info("{}", _runtime->LoadReport.Summary());
        const std::string failure = _runtime->LoadReport.FirstFailure();
        snprintf(error, maxlen, "%s", failure.c_str());
        OnUnload();
        Detail::SetRt(nullptr);
        _runtime.reset();
        return false;
    }

    Log::Info("{}", _runtime->LoadReport.Summary());
    if (_info.Commit != nullptr && *_info.Commit != '\0')
        Log::Info("Loaded {} v{} ({}, committed {}){}.", _info.Name, _info.Version, _info.Commit, _info.Date,
                  late ? " (late)" : "");
    else
        Log::Info("Loaded successfully{}.", late ? " (late)" : "");
    return true;
}

bool MetamodPlugin::Unload(char* error, size_t maxlen)
{
    // Order matters: the plugin drops its own graph first, while every kit service it holds a
    // reference or subscription to is still alive; then the standard hooks come off; only then
    // the runtime, whose destructor is the kit's shutdown. The ambient pointer is cleared last
    // of the three so teardown can still reach it.
    OnUnload();
    _standardHooks.clear();
    Detail::SetRt(nullptr);
    _runtime.reset();
    return true;
}

bool MetamodPlugin::OnPlayerChat(Players::Player* player, std::string_view message, bool /*teamChat*/)
{
    return _runtime->Commands.HandleChatMessage(player, message);
}

void MetamodPlugin::RegisterStandardHooks()
{
    auto& gi = _runtime->Interfaces;

    // CS2KIT_SCOPED_HOOK installs each hook and yields the Subscription that removes it, so the
    // add and remove lists cannot drift apart. Reset in Unload, before the runtime goes away.
    _standardHooks.push_back(CS2KIT_SCOPED_HOOK(IServerGameDLL, GameFrame, gi.ServerGameDLL,
                                                SH_MEMBER(this, &MetamodPlugin::Hook_GameFrame), true));
    _standardHooks.push_back(CS2KIT_SCOPED_HOOK(INetworkServerService, StartupServer, gi.NetworkServerService,
                                                SH_MEMBER(this, &MetamodPlugin::Hook_StartupServer), true));
    _standardHooks.push_back(CS2KIT_SCOPED_HOOK(IServerGameClients, OnClientConnected, gi.ServerGameClients,
                                                SH_MEMBER(this, &MetamodPlugin::Hook_OnClientConnected), false));
    _standardHooks.push_back(CS2KIT_SCOPED_HOOK(IServerGameClients, ClientDisconnect, gi.ServerGameClients,
                                                SH_MEMBER(this, &MetamodPlugin::Hook_ClientDisconnect), true));
    _standardHooks.push_back(CS2KIT_SCOPED_HOOK(IServerGameClients, ClientFullyConnect, gi.ServerGameClients,
                                                SH_MEMBER(this, &MetamodPlugin::Hook_ClientFullyConnect), true));
    _standardHooks.push_back(CS2KIT_SCOPED_HOOK(IServerGameClients, ClientSettingsChanged, gi.ServerGameClients,
                                                SH_MEMBER(this, &MetamodPlugin::Hook_ClientSettingsChanged), true));
    _standardHooks.push_back(CS2KIT_SCOPED_HOOK(ICvar, DispatchConCommand, gi.CVar,
                                                SH_MEMBER(this, &MetamodPlugin::Hook_DispatchConCommand), false));
    // Post hook: the game has filled the per-client transmit bitvecs; the filter clears bits.
    _standardHooks.push_back(CS2KIT_SCOPED_HOOK(ISource2GameEntities, CheckTransmit, gi.GameEntities,
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
    _runtime->CurrentMap = mapName ? mapName : "";
    _runtime->Events.OnServerStartup();
    _runtime->Teleports.OnServerStartup();
    _runtime->ClientCvars.OnServerStartup();
    OnServerStartup(mapName ? mapName : "");
}

void MetamodPlugin::Hook_CheckTransmit(CCheckTransmitInfo** infoList, int infoCount, CBitVec<16384>&, CBitVec<16384>&,
                                       const Entity2Networkable_t**, const uint16*, int)
{
    _runtime->Transmit.OnCheckTransmit(infoList, infoCount);
}

void MetamodPlugin::Hook_OnClientConnected(CPlayerSlot slot, const char* name, uint64 xuid, const char* networkId,
                                           const char* address, bool fakePlayer)
{
    int slotIdx = slot.Get();
    int64_t steamId = static_cast<int64_t>(xuid);
    Player* player = _runtime->Players.AddPlayer(slotIdx, steamId, name ? name : "", address ? address : "");
    OnPlayerConnect(player);
}

void MetamodPlugin::Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* name,
                                          uint64 xuid, const char* networkId)
{
    int slotIdx = slot.Get();
    OnPlayerDisconnect(_runtime->Players.GetPlayerBySlot(slotIdx));
    _runtime->OnPlayerDisconnect(slotIdx);
    _runtime->Players.RemovePlayer(slotIdx);
}

void MetamodPlugin::Hook_ClientFullyConnect(CPlayerSlot slot)
{
    _runtime->ClientCvars.OnClientFullyConnect(slot.Get());
    OnPlayerFullyConnected(_runtime->Players.GetPlayerBySlot(slot.Get()));
}

void MetamodPlugin::Hook_ClientSettingsChanged(CPlayerSlot slot)
{
    OnPlayerSettingsChanged(_runtime->Players.GetPlayerBySlot(slot.Get()));
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
    if (!Core::IsValidSlot(slotIdx))
        return;

    Player* player = _runtime->Players.GetPlayerBySlot(slotIdx);
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
    // Metamod asks every loaded plugin on each MetaFactory query, so an unknown iface is
    // routine, not an error. After Unload the container is gone - that is how peers see us go.
    void* impl = _runtime ? _runtime->Exchange.Find(iface) : nullptr;

    if (ret)
    {
        *ret = impl ? META_IFACE_OK : META_IFACE_FAILED;
    }

    return impl;
}

}  // namespace CS2Kit::App
