#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The engine hooks the host installed once, raised on every plugin in load order.
 *
 * Game thread only. A callback must not throw: nothing may unwind across the boundary.
 */
struct IHostEvents
{
    using FrameFn = void (*)(void* context);
    using ServerStartupFn = void (*)(void* context, std::string_view mapName);
    /** True refuses the player: later plugins are not asked, and the engine shows @p reason, which
     *  the callback fills with at most @p reasonSize bytes including the terminator. */
    using ClientConnectingFn = bool (*)(void* context, int slot, int64_t steamId, std::string_view name, char* reason,
                                        size_t reasonSize);
    using ClientConnectedFn = void (*)(void* context, int slot, int64_t steamId, std::string_view name,
                                       std::string_view address);
    using ClientDisconnectedFn = void (*)(void* context, int slot);
    using ClientFullyConnectedFn = void (*)(void* context, int slot);
    using ClientSettingsChangedFn = void (*)(void* context, int slot);
    /** True when this plugin answered it: later plugins do not see it and the engine call is blocked. */
    using ConsoleCommandFn = bool (*)(void* context, std::string_view name, std::string_view arguments, int slot);
    using CheckTransmitFn = void (*)(void* context, CCheckTransmitInfo** infoList, int infoCount);
    /** The next map's resource manifest, which only takes resources during this call. */
    using BuildGameSessionManifestFn = void (*)(void* context, IEntityResourceManifest* manifest);

    virtual uint64_t OnFrame(FrameFn callback, void* context) = 0;
    virtual uint64_t OnServerStartup(ServerStartupFn callback, void* context) = 0;
    /** Before the engine admits a player, and before OnClientConnected. */
    virtual uint64_t OnClientConnecting(ClientConnectingFn callback, void* context) = 0;
    virtual uint64_t OnClientConnected(ClientConnectedFn callback, void* context) = 0;
    virtual uint64_t OnClientDisconnected(ClientDisconnectedFn callback, void* context) = 0;
    virtual uint64_t OnClientFullyConnected(ClientFullyConnectedFn callback, void* context) = 0;
    virtual uint64_t OnClientSettingsChanged(ClientSettingsChangedFn callback, void* context) = 0;
    virtual uint64_t OnConsoleCommand(ConsoleCommandFn callback, void* context) = 0;
    virtual uint64_t OnCheckTransmit(CheckTransmitFn callback, void* context) = 0;
    virtual uint64_t OnBuildGameSessionManifest(BuildGameSessionManifestFn callback, void* context) = 0;

    /** Takes the token a subscription returned, which is never zero. Safe during a dispatch. */
    virtual void Unsubscribe(uint64_t token) = 0;

protected:
    ~IHostEvents() = default;
};

}  // namespace VoltMod
