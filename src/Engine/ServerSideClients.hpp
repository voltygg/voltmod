#pragma once

#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>

namespace VoltMod
{

/**
 * @file ServerSideClients.hpp
 * @brief The engine's connected-client objects, for the two hooks that need one.
 *
 * `CServerSideClient` is the per-connection object `Hooks::HudClicks` and `Addons` hook, but the
 * SDK's `INetworkGameServer` exposes no accessor for it, so the vector is reached by a gamedata
 * offset into `CNetworkGameServer`. Deliberately internal to `src/`: a plugin has `PlayerRef` and
 * `Player` for everything it should be doing with a connection, and nothing in the public API
 * needs a raw client pointer.
 *
 * Both accessors return nullptr rather than asserting when the server is down, the offset did not
 * bind, or the client is already torn down.
 */

/** Any connected client, for bootstrapping a hook that needs a live instance. */
void* AnyServerSideClient(const Interfaces& interfaces, const Bindings& bindings);

/** @p client's player slot, or -1 when it or the offset is unavailable. */
int SlotOfServerSideClient(const Bindings& bindings, const void* client);

}  // namespace VoltMod
