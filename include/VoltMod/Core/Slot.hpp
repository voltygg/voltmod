#pragma once

namespace VoltMod::Core
{

/** Maximum player slots on a CS2 server (slots 0..MaxPlayers-1). */
inline constexpr int MaxPlayers = 64;

/** True if @p slot is a valid player-slot index. */
inline constexpr bool IsValidSlot(int slot)
{
    return slot >= 0 && slot < MaxPlayers;
}

}  // namespace VoltMod::Core
