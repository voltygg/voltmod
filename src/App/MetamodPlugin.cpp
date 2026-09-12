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
    auto add = [this](Subscription hook) { _standardHooks.Add(std::move(hook)); };

    add(HookInterface(&IServerGameDLL::GameFrame, gi.ServerGameDLL, nullptr,
                      [this](IServerGameDLL&, bool, bool, bool) { _runtime->OnGameFrame(); }));

    add(HookInterface(&INetworkServerService::StartupServer, gi.NetworkServerService, nullptr,
                      [this](INetworkServerService&, const GameSessionConfiguration_t&, ISource2WorldSession*,
                             const char* mapName) { HandleServerStartup(mapName); }));

    add(HookInterface(&IServerGameClients::OnClientConnected, gi.ServerGameClients,
                      [this](IServerGameClients&, CPlayerSlot slot, const char* name, uint64 xuid, const char*,
                             const char* address, bool) {
                          // Here `name` is only a fallback, but `address` is available nowhere earlier.
                          _runtime->Players.Add(slot.Get(), static_cast<int64_t>(xuid), name ? name : "",
                                                address ? address : "");
                      }));

    add(HookInterface(&IServerGameClients::ClientDisconnect, gi.ServerGameClients, nullptr,
                      [this](IServerGameClients&, CPlayerSlot slot, ENetworkDisconnectionReason, const char*, uint64,
                             const char*) {
                          // Remove raises Disconnected before the slot is reused.
                          _runtime->Players.Remove(slot.Get());
                      }));

    add(HookInterface(&IServerGameClients::ClientFullyConnect, gi.ServerGameClients, nullptr,
                      [this](IServerGameClients&, CPlayerSlot slot) {
                          _runtime->Hooks.ClientConVars.OnClientFullyConnect(slot.Get());
                          _runtime->Players.OnClientFullyConnected(slot.Get());
                      }));

    add(HookInterface(&IServerGameClients::ClientSettingsChanged, gi.ServerGameClients, nullptr,
                      [this](IServerGameClients&, CPlayerSlot slot) {
                          _runtime->Players.OnClientSettingsChanged(slot.Get());
                      }));

    add(HookInterface(&ICvar::DispatchConCommand, gi.CVar,
                      [this](ICvar&, ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args) {
                          return HandleConCommand(cmd, ctx, args);
                      }));

    // The post hook filters the bit vectors after the game fills them.
    add(HookInterface(&ISource2GameEntities::CheckTransmit, gi.GameEntities, nullptr,
                      [this](ISource2GameEntities&, CCheckTransmitInfo** infoList, int infoCount, CBitVec<16384>&,
                             CBitVec<16384>&, const Entity2Networkable_t**, const uint16*, int) {
                          _runtime->Hooks.Visibility.OnCheckTransmit(infoList, infoCount);
                      }));

    Log::Info("Hooks registered.");
}

void MetamodPlugin::HandleServerStartup(const char* mapName)
{
    Log::Info("Server startup: map '{}'.", mapName ? mapName : "<none>");
    _runtime->Map.SetCurrent(mapName ? mapName : "");
    // Publish the new entity system before calling the plugin callback.
    _runtime->Entities.OnServerStartup();
    _runtime->GameEvents.OnServerStartup();
    _runtime->Hooks.ClientConVars.OnServerStartup();
    OnServerStartup(mapName ? std::string_view(mapName) : std::string_view{});
}

HookResult<void> MetamodPlugin::HandleConCommand(ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args)
{
    const char* cmdName = cmd.GetName();
    if (!cmdName)
        return {};

    bool isSay = (strcmp(cmdName, "say") == 0);
    bool isSayTeam = (strcmp(cmdName, "say_team") == 0);
    if (!isSay && !isSayTeam)
        return {};

    if (args.ArgC() < 2)
        return {};

    std::string_view message = args.Arg(1);
    if (message.size() >= 2 && message.front() == '"' && message.back() == '"')
    {
        message.remove_prefix(1);
        message.remove_suffix(1);
    }
    if (message.empty())
        return {};

    int slotIdx = ctx.GetPlayerSlot().Get();
    if (!IsValidSlot(slotIdx))
        return {};

    Player* player = _runtime->Players.Get(slotIdx);
    if (!player)
        return {};

    // Swallowing the command keeps a handled chat line out of the game's own say handler.
    if (OnPlayerChat(player, message, isSayTeam))
        return HookResult<void>::Block();
    return {};
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
