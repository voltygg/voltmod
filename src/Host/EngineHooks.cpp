#include "Host/EngineHooks.hpp"

#include "Host/EngineInterfaces.hpp"
#include "Host/Plugins/PluginHost.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Unsafe/Hook.hpp>
#include <eiface.h>
#include <icvar.h>
#include <interfaces/interfaces.h>
#include <iserver.h>
#include <string_view>
#include <tier1/convar.h>
#include <utility>

// KHook needs a complete type for this by-reference hook parameter.
class GameSessionConfiguration_t
{};

namespace VoltMod
{

EngineHooks::EngineHooks(PluginHost& host, std::function<void()> beforeFrame, std::function<void()> beforeServerStartup)
    : _host(host), _beforeFrame(std::move(beforeFrame)), _beforeServerStartup(std::move(beforeServerStartup))
{}

EngineHooks::~EngineHooks()
{
    Uninstall();
}

Status EngineHooks::Install(SourceMM::ISmmAPI* metamod)
{
    if (metamod == nullptr)
        return std::unexpected(Error::NotReady("the host has no Metamod API to resolve interfaces from"));

    auto fromEngine = EngineInterfaces(metamod);
    auto fromServer = ServerInterfaces(metamod);

    IServerGameDLL* serverGameDLL = nullptr;
    IServerGameClients* serverGameClients = nullptr;
    INetworkServerService* networkServerService = nullptr;
    ISource2GameEntities* gameEntities = nullptr;
    ICvar* cvar = nullptr;

    if (Status found = ResolveInterface(serverGameDLL, fromServer, INTERFACEVERSION_SERVERGAMEDLL); !found)
        return found;
    if (Status found = ResolveInterface(serverGameClients, fromServer, INTERFACEVERSION_SERVERGAMECLIENTS); !found)
        return found;
    if (Status found = ResolveInterface(networkServerService, fromEngine, NETWORKSERVERSERVICE_INTERFACE_VERSION);
        !found)
        return found;
    if (Status found = ResolveInterface(gameEntities, fromServer, INTERFACEVERSION_SERVERGAMEENTS); !found)
        return found;
    if (Status found = ResolveInterface(cvar, fromEngine, CVAR_INTERFACE_VERSION); !found)
        return found;

    // Register the host's own pending tier1 ConCommands - `volt` is one - before the engine can
    // dispatch them. Each plugin library still registers its own.
    g_pCVar = cvar;
    ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);

    auto add = [this](Subscription hook) { _hooks.Add(std::move(hook)); };

    add(HookInterface(&IServerGameDLL::GameFrame, serverGameDLL, nullptr, [this](IServerGameDLL&, bool, bool, bool) {
        if (_beforeFrame)
            _beforeFrame();
        _host.RaiseFrame();
    }));

    add(HookInterface(
        &INetworkServerService::StartupServer, networkServerService, nullptr,
        [this](INetworkServerService&, const GameSessionConfiguration_t&, ISource2WorldSession*, const char* mapName) {
            const std::string_view map = mapName != nullptr ? mapName : "";
            Log::Info("Server startup: map '{}'.", map.empty() ? std::string_view("<none>") : map);
            if (_beforeServerStartup)
                _beforeServerStartup();
            _host.RaiseServerStartup(map);
        }));

    add(HookInterface(&IServerGameClients::OnClientConnected, serverGameClients,
                      [this](IServerGameClients&, CPlayerSlot slot, const char* name, uint64 xuid, const char*,
                             const char* address, bool) {
                          // Before the call, while the engine still provides the connection address.
                          _host.RaiseClientConnected(slot.Get(), static_cast<int64_t>(xuid),
                                                     name != nullptr ? name : "", address != nullptr ? address : "");
                      }));

    add(HookInterface(
        &IServerGameClients::ClientDisconnect, serverGameClients, nullptr,
        [this](IServerGameClients&, CPlayerSlot slot, ENetworkDisconnectionReason, const char*, uint64, const char*) {
            // After the call, so a plugin sees the disconnect before the slot is reused.
            _host.RaiseClientDisconnected(slot.Get());
        }));

    add(HookInterface(&IServerGameClients::ClientFullyConnect, serverGameClients, nullptr,
                      [this](IServerGameClients&, CPlayerSlot slot) { _host.RaiseClientFullyConnected(slot.Get()); }));

    add(HookInterface(&IServerGameClients::ClientSettingsChanged, serverGameClients, nullptr,
                      [this](IServerGameClients&, CPlayerSlot slot) { _host.RaiseClientSettingsChanged(slot.Get()); }));

    add(HookInterface(&ICvar::DispatchConCommand, cvar,
                      [this](ICvar&, ConCommandRef command, const CCommandContext& context, const CCommand& arguments) {
                          const char* name = command.GetName();
                          if (name == nullptr)
                              return HookResult<void>{};

                          // ArgS is the whole line after the command; a plugin parses what it wants.
                          const char* line = arguments.ArgS();
                          if (_host.RaiseConsoleCommand(name, line != nullptr ? line : "",
                                                        context.GetPlayerSlot().Get()))
                              return HookResult<void>::Block();
                          return HookResult<void>{};
                      }));

    // Filter the bit vectors after the game fills them.
    add(HookInterface(
        &ISource2GameEntities::CheckTransmit, gameEntities, nullptr,
        [this](ISource2GameEntities&, CCheckTransmitInfo** infoList, int infoCount, CBitVec<16384>&, CBitVec<16384>&,
               const Entity2Networkable_t**, const uint16*, int) { _host.RaiseCheckTransmit(infoList, infoCount); }));

    Log::Info("Engine hooks installed.");
    return {};
}

void EngineHooks::Uninstall()
{
    _hooks.Clear();
}

}  // namespace VoltMod
