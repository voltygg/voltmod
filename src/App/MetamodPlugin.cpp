#include <VoltMod/App/MetamodPlugin.hpp>
#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Unsafe/Hook.hpp>
#include <cstdio>
#include <cstring>
#include <format>
#include <iserver.h>
#include <string>
#include <string_view>
#include <vector>

// PLUGIN_GLOBALVARS is defined by each plugin's VOLTMOD_PLUGIN.

// KHook needs a complete type for this by-reference hook parameter.
class GameSessionConfiguration_t
{};

namespace VoltMod
{

/** Let the engine's own handler run unchanged. */
constexpr KHook::Return<void> Pass{KHook::Action::Ignore};

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
    OnRegisterHooks(*_runtime, _customHooks);

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
    if (!_info.Commit.empty())
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

    // Each hook owns its removal subscription; clear them before the runtime.
    _standardHooks.Add(HookInterface(&IServerGameDLL::GameFrame, gi.ServerGameDLL, this, nullptr,
                                     &MetamodPlugin::Hook_GameFrame));
    _standardHooks.Add(HookInterface(&INetworkServerService::StartupServer, gi.NetworkServerService, this, nullptr,
                                     &MetamodPlugin::Hook_StartupServer));
    _standardHooks.Add(HookInterface(&IServerGameClients::OnClientConnected, gi.ServerGameClients, this,
                                     &MetamodPlugin::Hook_OnClientConnected, nullptr));
    _standardHooks.Add(HookInterface(&IServerGameClients::ClientDisconnect, gi.ServerGameClients, this, nullptr,
                                     &MetamodPlugin::Hook_ClientDisconnect));
    _standardHooks.Add(HookInterface(&IServerGameClients::ClientFullyConnect, gi.ServerGameClients, this, nullptr,
                                     &MetamodPlugin::Hook_ClientFullyConnect));
    _standardHooks.Add(HookInterface(&IServerGameClients::ClientSettingsChanged, gi.ServerGameClients, this, nullptr,
                                     &MetamodPlugin::Hook_ClientSettingsChanged));
    _standardHooks.Add(HookInterface(&ICvar::DispatchConCommand, gi.CVar, this,
                                     &MetamodPlugin::Hook_DispatchConCommand, nullptr));
    // The post hook filters the bit vectors after the game fills them.
    _standardHooks.Add(HookInterface(&ISource2GameEntities::CheckTransmit, gi.GameEntities, this, nullptr,
                                     &MetamodPlugin::Hook_CheckTransmit));

    Log::Info("Hooks registered.");
}

KHook::Return<void> MetamodPlugin::Hook_GameFrame(IServerGameDLL*, bool simulating, bool firstTick, bool lastTick)
{
    _runtime->OnGameFrame();
    return Pass;
}

KHook::Return<void> MetamodPlugin::Hook_StartupServer(INetworkServerService*, const GameSessionConfiguration_t&,
                                                      ISource2WorldSession*, const char* mapName)
{
    Log::Info("Server startup: map '{}'.", mapName ? mapName : "<none>");
    _runtime->Map.SetCurrent(mapName ? mapName : "");
    // Publish the new entity system before calling the plugin callback.
    _runtime->Entities.OnServerStartup();
    _runtime->GameEvents.OnServerStartup();
    _runtime->Hooks.Teleport.OnServerStartup();
    _runtime->Hooks.ClientConVars.OnServerStartup();
    OnServerStartup(mapName ? std::string_view(mapName) : std::string_view{});
    return Pass;
}

KHook::Return<void> MetamodPlugin::Hook_CheckTransmit(ISource2GameEntities*, CCheckTransmitInfo** infoList,
                                                      int infoCount, CBitVec<16384>&, CBitVec<16384>&,
                                                      const Entity2Networkable_t**, const uint16*, int)
{
    _runtime->Hooks.Visibility.OnCheckTransmit(infoList, infoCount);
    return Pass;
}

KHook::Return<void> MetamodPlugin::Hook_OnClientConnected(IServerGameClients*, CPlayerSlot slot, const char* name,
                                                          uint64 xuid, const char* networkId, const char* address,
                                                          bool fakePlayer)
{
    // At this callback `name` is only a fallback; `address` is available here first.
    _runtime->Players.Add(slot.Get(), static_cast<int64_t>(xuid), name ? name : "", address ? address : "");
    return Pass;
}

KHook::Return<void> MetamodPlugin::Hook_ClientDisconnect(IServerGameClients*, CPlayerSlot slot,
                                                         ENetworkDisconnectionReason reason, const char* name,
                                                         uint64 xuid, const char* networkId)
{
    // Remove raises Disconnected before the slot is reused.
    _runtime->Players.Remove(slot.Get());
    return Pass;
}

KHook::Return<void> MetamodPlugin::Hook_ClientFullyConnect(IServerGameClients*, CPlayerSlot slot)
{
    _runtime->Hooks.ClientConVars.OnClientFullyConnect(slot.Get());
    _runtime->Players.OnClientFullyConnected(slot.Get());
    return Pass;
}

KHook::Return<void> MetamodPlugin::Hook_ClientSettingsChanged(IServerGameClients*, CPlayerSlot slot)
{
    _runtime->Players.OnClientSettingsChanged(slot.Get());
    return Pass;
}

KHook::Return<void> MetamodPlugin::Hook_DispatchConCommand(ICvar*, ConCommandRef cmd, const CCommandContext& ctx,
                                                           const CCommand& args)
{
    const char* cmdName = cmd.GetName();
    if (!cmdName)
        return Pass;

    bool isSay = (strcmp(cmdName, "say") == 0);
    bool isSayTeam = (strcmp(cmdName, "say_team") == 0);
    if (!isSay && !isSayTeam)
        return Pass;

    if (args.ArgC() < 2)
        return Pass;

    std::string_view message = args.Arg(1);
    if (message.size() >= 2 && message.front() == '"' && message.back() == '"')
    {
        message.remove_prefix(1);
        message.remove_suffix(1);
    }
    if (message.empty())
        return Pass;

    int slotIdx = ctx.GetPlayerSlot().Get();
    if (!IsValidSlot(slotIdx))
        return Pass;

    Player* player = _runtime->Players.Get(slotIdx);
    if (!player)
        return Pass;

    // Swallowing the command keeps a handled chat line out of the game's own say handler.
    if (OnPlayerChat(player, message, isSayTeam))
        return {KHook::Action::Supersede};
    return Pass;
}

const char* MetamodPlugin::GetAuthor()
{
    return _info.Author.c_str();
}
const char* MetamodPlugin::GetName()
{
    return _info.Name.c_str();
}
const char* MetamodPlugin::GetDescription()
{
    return _info.Description.c_str();
}
const char* MetamodPlugin::GetURL()
{
    return _info.Url.c_str();
}
const char* MetamodPlugin::GetLicense()
{
    return _info.License.c_str();
}
const char* MetamodPlugin::GetVersion()
{
    return _info.Version.c_str();
}
const char* MetamodPlugin::GetDate()
{
    return _info.Date.c_str();
}
const char* MetamodPlugin::GetLogTag()
{
    return _info.LogTag.c_str();
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
