#pragma once

#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/ObserverMode.hpp>
#include <VoltMod/Schema/Generated/CCSPlayerPawn.hpp>
#include <cstdint>
#include <string>

namespace VoltMod
{

/**
 * @brief A player's body: health, armor, movement, aim. The engine replaces it on every spawn;
 * the @ref Controller is the identity that stays.
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

    /** Where shots start: the origin plus the view offset. */
    Vector EyePosition() const;

    Schema::MoveType_t Move() const { return MoveTypeRaw(); }

    /** Writes both move-type fields; the engine reverts a lone one next tick. */
    void SetMove(Schema::MoveType_t type) const;

    /** @return Error::Unsupported when the CommitSuicide index did not bind. */
    Status Slay() const;

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
