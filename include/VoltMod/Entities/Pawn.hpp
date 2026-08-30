#pragma once

#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/MoveType.hpp>
#include <VoltMod/Entities/ObserverMode.hpp>
#include <VoltMod/Entities/Render.hpp>
#include <VoltMod/Schema/Generated/CCSPlayerPawn.hpp>
#include <cstdint>
#include <string>

namespace VoltMod
{

/**
 * @brief A player's pawn - the body that stands in the world, holds the weapons, and takes the
 * damage.
 *
 * This is where a player's vitals live: health, armor, movement, aim, flash state. The
 * @ref Controller is the persistent scoreboard identity and outlives the pawn across deaths and
 * team changes; the pawn is replaced on every spawn.
 *
 * Frame-local, like every wrapper: see @ref Entity for the validity contract.
 */
class Pawn : public Entity
{
public:
    using Entity::Entity;

    /** @name CCSPlayerPawn, CBasePlayerPawn and CBaseModelEntity fields.
     *
     *  Generated from `schema/manifest.json`. Notes worth keeping in mind:
     *  - `EyeAngles` is declared on CCSPlayerPawn, not on CCSPlayerPawnBase.
     *  - `SpeedModifier` decays toward 1.0 (e.g. after firing), so it is a nudge, not a setting.
     *  - `GroundEntity` is @ref InvalidEntityHandle when airborne.
     *  - `FlashMaxAlpha` of 255 means the last flash was a full blind; for blind-time bookkeeping
     *    prefer the typed `PlayerBlind` game event, which carries the duration directly.
     *  - `ViewOffset` reads the leading Vector of a 40-byte CNetworkViewOffsetVector.
     *  - `RenderColor` is RGBA; the low byte is R and the high byte is A.
     */
    /** @{ */
#include <VoltMod/Schema/Generated/Wrappers/Pawn.inc>
    /** @} */

    [[nodiscard]] bool IsAlive() const { return _e != nullptr && LifeState() == 0; }

    /** Where this pawn's shots originate: the origin plus @ref ViewOffset. */
    [[nodiscard]] Vector EyePosition() const;

    [[nodiscard]] MoveType Move() const { return static_cast<MoveType>(MoveTypeRaw()); }

    /** Writes both `m_MoveType` and `m_nActualMoveType`; setting only one lets the engine revert
     *  it on the next tick. */
    void SetMove(MoveType type) const;

    /** Kill the pawn through `CBasePlayerPawn::CommitSuicide`.
     *  @return Error::Unsupported when the vtable index did not bind. */
    Status Slay() const;

    /** Read `m_iObserverMode` from the pawn's CPlayer_ObserverServices. The services pointer is
     *  its own object, so this is a method rather than a Field. */
    [[nodiscard]] ObserverMode_t GetObserverMode() const;
    Status SetObserverMode(ObserverMode_t mode) const;

    /** Current model path (the scene node's CModelState). Empty when unavailable. */
    [[nodiscard]] std::string ModelName() const;

    /**
     * Apply transparency to the pawn body. Weapons and wearables are unaffected - CS2 routes
     * those through systems a server plugin cannot reach. For real invisibility use the transmit
     * filter (@ref Transmit) instead.
     *
     * @param visible true restores the opaque default; false hides the body.
     * @param alpha   alpha byte applied when @p visible is false. 0 is fully invisible.
     */
    void SetVisible(bool visible, uint8_t alpha = 0) const;

    /** Set render mode and color together, dirtying both for replication. */
    void SetRender(RenderMode_t mode, uint32_t color) const;

    /** The controller that owns this pawn, resolved through `m_hController`. */
    [[nodiscard]] Controller GetController() const;

    /** Slot of the owning player, or -1. Constant-time: it reads the pawn's own back-reference
     *  rather than scanning the roster, which per-damage and per-tick paths depend on. */
    [[nodiscard]] int Slot() const;
};

}  // namespace VoltMod
