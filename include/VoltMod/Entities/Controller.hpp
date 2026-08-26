#pragma once

#include <VoltMod/Entities/Pawn.hpp>
#include <cstdint>
#include <string_view>

namespace VoltMod
{

/**
 * @brief A player's controller - the persistent identity behind a slot.
 *
 * The controller owns the name, the scoreboard row and the money; it survives death, team changes
 * and respawns. Everything about the body - health, armor, position, movement - is on the
 * @ref Pawn, which the engine replaces on every spawn. `controller.GetPawn().Health` is the
 * player's health; the CBaseEntity fields this inherits from @ref Entity are the controller
 * entity's own and mean nothing for gameplay.
 *
 * Frame-local, like every wrapper: see @ref Entity for the validity contract.
 */
class Controller : public Entity
{
public:
    Controller() = default;

    /** Resolves and caches the possessed pawn, so @ref GetPawn is free afterwards. Prefer
     *  `runtime.Entities.Controller(slot)`. */
    Controller(EntitySystem& entities, CEntityInstance* raw, int slot);

    Controller(const Controller&) = default;
    Controller& operator=(const Controller&) = delete;

    /** Scoreboard name (`m_iszPlayerName`, a 128-byte fixed buffer). Assigning truncates to 127
     *  characters plus NUL. Replication piggybacks on the next state-change broadcast, so pair a
     *  write with @ref ChangeTeam or similar when the scoreboard has to refresh now. */
    Field<CharBuf<128>, "CBasePlayerController", "m_iszPlayerName"> Name{_e};

    /** The slot this controller occupies, or -1. */
    [[nodiscard]] int Slot() const noexcept { return _slot; }

    /** The pawn this player currently possesses, resolved once when the wrapper was built. */
    [[nodiscard]] Pawn GetPawn() const;

    /** Buy-menu balance (CCSPlayerController_InGameMoneyServices::m_iAccount), or 0 when the money
     *  services are unavailable. */
    [[nodiscard]] int Money() const;

    /** Write the balance and dirty it, so the client's HUD follows.
     *  @return Error::NotReady when the money services are unavailable. */
    Status SetMoney(int amount) const;

    /** Disconnect the client. @return Error::NotReady when IVEngineServer2 is unavailable. */
    Status Kick(std::string_view reason) const;

    /** `CCSPlayerController::ChangeTeam`. @ref PawnOps::ChangeTeamSafe bounds-checks @p team.
     *  @return Error::Unsupported when the vtable index did not bind. */
    Status ChangeTeam(int team) const;

    /** `CCSPlayerController::Respawn`. @return Error::Unsupported when the index did not bind. */
    Status Respawn() const;

private:
    int _slot = -1;
    /** Resolved in the constructor: the pawn lookup is a handle resolve, and callers ask for it
     *  several times per wrapper. */
    CEntityInstance* _pawn = nullptr;
};

}  // namespace VoltMod
