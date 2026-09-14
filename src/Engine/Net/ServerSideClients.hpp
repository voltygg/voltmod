#pragma once

#include <VoltMod/Engine/GameData/Bindings.hpp>

namespace VoltMod
{

/**
 * @file ServerSideClients.hpp
 * @brief The engine's connected-client objects, for the hooks that run on one.
 *
 * `CServerSideClient` is the per-connection object `ButtonPressHook` and `Addons` hook into. The SDK
 * exposes no accessor for it, so it is reached by gamedata offsets. Deliberately internal to
 * `src/`: a plugin has `PlayerRef` and `Player` for everything it should be doing with a
 * connection, and nothing in the public API needs a raw client pointer.
 */

/** The client @p filter is the `INetworkMessageProcessingPreFilter` base of; nullptr if the offset did not bind. */
const void* ClientOfFilter(const Bindings& bindings, const EngineMessageFilter& filter);

/** @p client's player slot, or -1 when it or the offset is unavailable. */
int SlotOfClient(const Bindings& bindings, const void* client);

}  // namespace VoltMod
