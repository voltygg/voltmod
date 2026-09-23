#pragma once

#include <VoltMod/Engine/GameData/Bindings.hpp>

namespace VoltMod
{

/** Return the client containing @p filter, or nullptr when its offset is unavailable. */
const void* ClientOfFilter(const Bindings& bindings, const INetworkMessageProcessingPreFilter& filter);

/** @p client's player slot, or -1 when it or the offset is unavailable. */
int SlotOfClient(const Bindings& bindings, const void* client);

}  // namespace VoltMod
