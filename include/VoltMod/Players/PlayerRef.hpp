#pragma once

#include <cstdint>

namespace VoltMod
{

/**
 * @brief A storable reference to a player: the slot plus the SteamID that occupied it.
 *
 * @ref Controller and @ref Pawn are frame-local, and a bare slot number is not enough on its own -
 * a slot changes hands, so work scheduled against one can land on whoever sits there next. Keep a
 * PlayerRef instead and check both halves when it is resolved: the slot says where to look and the
 * SteamID says whether it is still the same player.
 *
 * `SteamId == 0` means a bot, which no reference can pin down across a reconnect.
 */
struct PlayerRef
{
    int Slot = -1;
    int64_t SteamId = 0;

    explicit operator bool() const noexcept { return Slot >= 0; }
    bool operator==(const PlayerRef&) const noexcept = default;
};

}  // namespace VoltMod
