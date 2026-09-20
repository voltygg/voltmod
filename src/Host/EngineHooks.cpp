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

/** The engine passes null for an absent string. */
static std::string_view Text(const char* text)
{
    return text != nullptr ? text : "";
}

/** Offer a command to the plugins, and keep it from the engine when one consumes it. */
static HookResult<void> RunCommand(PluginHost& host, std::string_view name, const CCommand& arguments, int slot)
{
    // ArgS is the whole line after the command name.
    const bool consumed = host.RaiseConsoleCommand(name, Text(arguments.ArgS()), slot);
    return consumed ? HookResult<void>::Block() : HookResult<void>{};
}

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

    // Stops at the first failure.
    Status found;
    auto resolve = [&found](auto*& target, auto& factory, const char* version) {
        if (found)
            found = ResolveInterface(target, factory, version);
    };

    resolve(serverGameDLL, fromServer, INTERFACEVERSION_SERVERGAMEDLL);
    resolve(serverGameClients, fromServer, INTERFACEVERSION_SERVERGAMECLIENTS);
    resolve(networkServerService, fromEngine, NETWORKSERVERSERVICE_INTERFACE_VERSION);
    resolve(gameEntities, fromServer, INTERFACEVERSION_SERVERGAMEENTS);
    resolve(cvar, fromEngine, CVAR_INTERFACE_VERSION);

    if (!found)
        return found;

    // Registers the host's own ConCommands, `volt` among them.
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
            const std::string_view map = Text(mapName);
            Log::Info("Server startup: map '{}'.", map.empty() ? "<none>" : map);
            if (_beforeServerStartup)
                _beforeServerStartup();
            _host.RaiseServerStartup(map);
            DisconnectEveryone();
        }));

    add(HookInterface(&IServerGameClients::OnClientConnected, serverGameClients,
                      [this](IServerGameClients&, CPlayerSlot slot, const char* name, uint64 xuid, const char*,
                             const char* address, bool) {
                          // Before the call: the address is gone after it.
                          ConnectClient(slot.Get(), xuid, Text(name), Text(address));
                      }));

    // Clients returning from a map change skip OnClientConnected.
    add(HookInterface(&IServerGameClients::ClientPutInServer, serverGameClients, nullptr,
                      [this](IServerGameClients&, CPlayerSlot slot, const char* name, int, uint64 xuid) {
                          if (!IsValidSlot(slot.Get()))
                              return;
                          if (_connected[slot.Get()])
                              return;

                          std::string_view address;
                          auto known = _addresses.find(xuid);
                          if (known != _addresses.end())
                              address = known->second;
                          ConnectClient(slot.Get(), xuid, Text(name), address);
                      }));

    add(HookInterface(&IServerGameClients::ClientDisconnect, serverGameClients, nullptr,
                      [this](IServerGameClients&, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char*,
                             uint64 xuid, const char*) {
                          if (IsValidSlot(slot.Get()))
                              _connected[slot.Get()] = false;
                          // Kept over a map change: the client returns without it.
                          if (reason != NETWORK_DISCONNECT_LOOPSHUTDOWN)
                              _addresses.erase(xuid);
                          // After the call, before the slot is reused.
                          _host.RaiseClientDisconnected(slot.Get());
                      }));

    add(HookInterface(&IServerGameClients::ClientFullyConnect, serverGameClients, nullptr,
                      [this](IServerGameClients&, CPlayerSlot slot) { _host.RaiseClientFullyConnected(slot.Get()); }));

    add(HookInterface(&IServerGameClients::ClientSettingsChanged, serverGameClients, nullptr,
                      [this](IServerGameClients&, CPlayerSlot slot) { _host.RaiseClientSettingsChanged(slot.Get()); }));

    add(HookInterface(&ICvar::DispatchConCommand, cvar,
                      [this](ICvar&, ConCommandRef command, const CCommandContext& context, const CCommand& arguments) {
                          return RunCommand(_host, Text(command.GetName()), arguments, context.GetPlayerSlot().Get());
                      }));

    // Client commands that are not ConCommands, `vote` among them.
    add(HookInterface(&IServerGameClients::ClientCommand, serverGameClients,
                      [this](IServerGameClients&, CPlayerSlot slot, const CCommand& arguments) {
                          return RunCommand(_host, Text(arguments.Arg(0)), arguments, slot.Get());
                      }));

    // Filter the bit vectors after the game fills them.
    add(HookInterface(
        &ISource2GameEntities::CheckTransmit, gameEntities, nullptr,
        [this](ISource2GameEntities&, CCheckTransmitInfo** infoList, int infoCount, CBitVec<16384>&, CBitVec<16384>&,
               const Entity2Networkable_t**, const uint16*, int) { _host.RaiseCheckTransmit(infoList, infoCount); }));

    Log::Info("Engine hooks installed.");
    return {};
}

void EngineHooks::ConnectClient(int slot, uint64_t xuid, std::string_view name, std::string_view address)
{
    if (IsValidSlot(slot))
        _connected[slot] = true;
    if (xuid != 0 && !address.empty())
        _addresses[xuid] = address;
    _host.RaiseClientConnected(slot, static_cast<int64_t>(xuid), name, address);
}

void EngineHooks::DisconnectEveryone()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (!_connected[slot])
            continue;
        _connected[slot] = false;
        _host.RaiseClientDisconnected(slot);
    }
}

void EngineHooks::Uninstall()
{
    _hooks.Clear();
}

}  // namespace VoltMod
