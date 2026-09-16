#pragma once

#include <VoltMod/Engine/GameData/Bindings.hpp>

namespace VoltMod
{

/**
 * @file ServerSideClients.hpp
 * @brief Internal access to `CServerSideClient` for connection hooks.
 *
 * The SDK exposes no accessor, so these functions use gamedata offsets. Plugins use `PlayerRef`
 * and `Player` instead of raw client pointers.
 */

/** Return the client containing @p filter, or nullptr when its offset is unavailable. */
const void* ClientOfFilter(const Bindings& bindings, const EngineMessageFilter& filter);

/** @p client's player slot, or -1 when it or the offset is unavailable. */
int SlotOfClient(const Bindings& bindings, const void* client);

}  // namespace VoltMod
