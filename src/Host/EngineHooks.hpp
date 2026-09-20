#pragma once

#include "Host/Plugins/PluginHost.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/Subscriptions.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace VoltMod
{

/**
 * @brief The nine engine hooks, installed once for the whole process.
 *
 * Each one raises the matching event on @ref PluginHost, which calls every loaded plugin in load
 * order. Game thread only, like everything downstream of it.
 */
class EngineHooks
{
public:
    /**
     * @param beforeFrame Runs at the start of every frame, before the frame reaches the plugins.
     * @param beforeServerStartup Runs on every map change, before the plugins hear about it.
     */
    EngineHooks(PluginHost& host, std::function<void()> beforeFrame, std::function<void()> beforeServerStartup);
    ~EngineHooks();

    EngineHooks(const EngineHooks&) = delete;
    EngineHooks& operator=(const EngineHooks&) = delete;

    /** Resolve the engine interfaces the hooks need from @p metamod, then install them. */
    Status Install(SourceMM::ISmmAPI* metamod);

    /** Remove every hook. Nothing reaches the plugins after this returns. */
    void Uninstall();

private:
    /** Raise ClientConnected once per stay in a slot, remembering the address for a map change. */
    void ConnectClient(int slot, uint64_t xuid, std::string_view name, std::string_view address);

    /** A map change moves clients to new slots without disconnecting them; ClientPutInServer
     *  reconnects each one where it lands. Runs after the plugins re-read the new map. */
    void DisconnectEveryone();

    PluginHost& _host;
    std::function<void()> _beforeFrame;
    std::function<void()> _beforeServerStartup;
    Subscriptions _hooks;
    std::array<bool, MaxPlayers> _connected{};
    std::unordered_map<uint64_t, std::string> _addresses;  ///< by xuid, kept across a map change
};

}  // namespace VoltMod
