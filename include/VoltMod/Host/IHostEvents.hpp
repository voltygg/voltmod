#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Host/HostTypes.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief The engine hooks, installed once by the host and called on every loaded plugin.
 *
 * Callbacks run on the game thread in load order. A callback must not throw: the SDK trampoline
 * that owns it catches, logs and continues, and nothing propagates across the boundary.
 */
struct IHostEvents
{
    static constexpr const char* InterfaceName = "VoltMod.IHostEvents";

    using FrameFn = void (*)(void* context);
    using ServerStartupFn = void (*)(void* context, HostString mapName);
    using ClientConnectedFn = void (*)(void* context, int slot, int64_t steamId, HostString name, HostString address);
    using ClientDisconnectedFn = void (*)(void* context, int slot);
    using ClientFullyConnectedFn = void (*)(void* context, int slot);
    using ClientSettingsChangedFn = void (*)(void* context, int slot);
    /** True when this plugin answered the command: later plugins do not see it and the engine
     *  call is blocked once. */
    using ConsoleCommandFn = bool (*)(void* context, HostString name, HostString arguments, int slot);
    using CheckTransmitFn = void (*)(void* context, CCheckTransmitInfo** infoList, int infoCount);

    virtual uint64_t OnFrame(FrameFn callback, void* context) = 0;
    virtual uint64_t OnServerStartup(ServerStartupFn callback, void* context) = 0;
    virtual uint64_t OnClientConnected(ClientConnectedFn callback, void* context) = 0;
    virtual uint64_t OnClientDisconnected(ClientDisconnectedFn callback, void* context) = 0;
    virtual uint64_t OnClientFullyConnected(ClientFullyConnectedFn callback, void* context) = 0;
    virtual uint64_t OnClientSettingsChanged(ClientSettingsChangedFn callback, void* context) = 0;
    virtual uint64_t OnConsoleCommand(ConsoleCommandFn callback, void* context) = 0;
    virtual uint64_t OnCheckTransmit(CheckTransmitFn callback, void* context) = 0;

    /** Each subscription above returns the token that removes it, which is never zero while
     *  it is live. Safe during a dispatch: the one in flight skips the removed callback. */
    virtual void Unsubscribe(uint64_t token) = 0;

protected:
    ~IHostEvents() = default;
};

}  // namespace VoltMod
