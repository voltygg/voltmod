#pragma once

#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/ObserverMode.hpp>
#include <VoltMod/Schema/Generated/CCSPlayerPawn.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief A player's body: health, armor, movement, aim, weapons. The engine replaces it on every
 * spawn; the @ref Controller is the identity that stays.
 */
class Pawn : public Entity
{
public:
    using Entity::Entity;

    /** @name CCSPlayerPawn, CBasePlayerPawn and CBaseModelEntity fields
     *  `SpeedModifier` decays back toward 1 on its own, and `ViewOffset` is the eye height. */
    /** @{ */
#include <VoltMod/Schema/Generated/Wrappers/Pawn.inc>
    /** @} */

    bool IsAlive() const { return _e != nullptr && LifeState() == LIFE_ALIVE; }

    bool IsOnGround() const { return (Flags() & FL_ONGROUND) != 0; }

    /** Where shots start: the origin plus the view offset. */
    Vector EyePosition() const;

    /** @return Error::Unsupported when the CommitSuicide index did not bind. */
    Status Slay() const;

    /** The FL_GODMODE flag, which is what makes a CS2 pawn take no damage. */
    bool Godmode() const { return (Flags() & FL_GODMODE) != 0; }
    void SetGodmode(bool on) const;

    /** Throw the pawn with @p velocity, off the ground this tick. */
    void Launch(Vector velocity) const;

    /** Add @p amount health, up to MaxHealth. False when nothing changed, such as a dead pawn. */
    bool Heal(int amount) const;

    /** Give an item by class name, such as "weapon_ak47". A weapon the pawn's team cannot buy is
     *  given through a same-frame team swap. False when the engine refused it. */
    bool GiveItem(std::string_view item) const;

    /** Remove every weapon, and with @p removeSuit the armor and defuse kit too. */
    bool StripWeapons(bool removeSuit = true) const;

    /** The carried weapons, knife and grenades included. */
    std::vector<Entity> Weapons() const;

    /** Keep the weapon in hand from either attack before engine tick @p tick. A weapon switched to
     *  later is not covered, so call it every frame to hold fire for a while. */
    void HoldFire(int tick) const;

    VoltMod::ObserverMode ObserverMode() const;
    Status SetObserverMode(VoltMod::ObserverMode mode) const;

    /** Current model path; empty when unavailable. */
    std::string ModelName() const;

    /** Fade the body to @p alpha, or restore it. Held weapons and wearables stay visible; hide a
     *  player completely with @ref Visibility. */
    void SetVisible(bool visible, uint8_t alpha = 0) const;

    /** The owning player's controller. */
    VoltMod::Controller Controller() const;

    /** The owning player's slot, or -1. Constant time. */
    int Slot() const;
};

}  // namespace VoltMod
