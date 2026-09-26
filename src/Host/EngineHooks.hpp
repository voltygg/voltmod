#pragma once

#include "Host/Plugins/PluginHost.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/Subscriptions.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The engine hooks, installed once for the whole process.
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

    /** Resolve the engine interfaces the hooks need, then install them. */
    Status Install();

    /** Remove every hook. Nothing reaches the plugins after this returns. */
    void Uninstall();

private:
    /** Mark the slot connected and raise ClientConnected. */
    void ConnectClient(int slot, uint64_t xuid, std::string_view name, std::string_view address);

    /** A map change moves clients to new slots; ClientPutInServer reconnects them there. */
    void DisconnectEveryone();

    PluginHost& _host;
    std::function<void()> _beforeFrame;
    std::function<void()> _beforeServerStartup;
    Subscriptions _hooks;
    std::array<bool, MaxPlayers> _connected{};
};

}  // namespace VoltMod
