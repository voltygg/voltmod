#pragma once

#include <cstdint>

namespace CS2Kit::Players
{

class Player;
class PlayerManager;

/**
 * @brief A player identity that stays honest across a delay.
 *
 * A bare slot answers "who is in seat 4 right now", which is the wrong question for anything
 * that resumes later - a scheduled timer, a database completion, a menu the player is still
 * reading. By the time it runs, seat 4 may hold someone else, and the work lands on them:
 * a fall-protection timer stripping godmode from the newcomer, a punishment flow acting on
 * whoever took the slot.
 *
 * Pairing the slot with the occupancy generation makes that detectable. @ref PlayerManager
 * stamps a fresh generation every time a slot changes hands, so resolving a ref taken before
 * the swap yields nullptr instead of the wrong player.
 *
 * Take one with @ref Player::Ref, resolve it with @ref PlayerManager::Resolve, and resolve it
 * again on every use rather than caching the result - a ref is a claim to check, not a handle.
 *
 * Immediate call paths keep using `int slot`: there is no gap for anyone to slip through, and
 * a ref would only add ceremony.
 */
struct PlayerRef
{
    int Slot = -1;
    /** Occupancy stamp for @ref Slot at the moment the ref was taken. 0 = never valid. */
    uint64_t Generation = 0;
    /** Carried so an offline player is still addressable (bans, admin records). 0 for bots. */
    int64_t SteamId = 0;

    /** True if this ref ever named a player. Says nothing about whether they are still here. */
    [[nodiscard]] bool IsSet() const { return Generation != 0; }

    friend bool operator==(const PlayerRef&, const PlayerRef&) = default;
};

}  // namespace CS2Kit::Players
