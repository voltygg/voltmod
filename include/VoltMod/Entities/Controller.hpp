#pragma once

#include <VoltMod/Engine/Team.hpp>
#include <VoltMod/Entities/Pawn.hpp>
#include <VoltMod/Schema/Generated/CCSPlayerController.hpp>
#include <cstdint>
// The IN_* bits Buttons() returns.
#include <in_buttons.h>
#include <string_view>

namespace VoltMod
{

/**
 * @brief A player's identity behind a slot: name, money, team, scoreboard row. It survives death
 * and respawn; the body is @ref Pawn(). Its own CBaseEntity fields, such as Health, mean nothing
 * for gameplay.
 */
class Controller : public Entity
{
public:
    Controller() = default;

    /** Prefer `runtime.Entities.Controller(slot)`. */
    Controller(EntitySystem& entities, CEntityInstance* raw, int slot);

    Controller(const Controller&) = default;
    Controller& operator=(const Controller&) = delete;

    /** @name CCSPlayerController and CBasePlayerController fields
     *  `SetName` shows on the scoreboard with the next state change, such as @ref ChangeTeam. */
    /** @{ */
#include <VoltMod/Schema/Generated/Wrappers/Controller.inc>
    /** @} */

    /** The slot, or -1. */
    int Slot() const noexcept { return _slot; }

    /** The living body; falsy while dead. */
    VoltMod::Pawn Pawn() const;

    /** The pawn the player's input drives: @ref Pawn while alive, the observer pawn while dead or
     *  spectating. */
    VoltMod::Pawn InputPawn() const;

    /** Held buttons as `IN_*` bits, read from @ref InputPawn so they arrive while dead too. */
    uint64_t Buttons() const;

    /** The buy-menu balance; 0 when unavailable. */
    int Money() const;

    /** Does nothing when the money services are unavailable. */
    void SetMoney(int amount) const;

    void Kick(std::string_view reason) const;

    /** Run one console command as this player, server-side and without a chat echo; plugins
     *  hooking ISource2GameClients::ClientCommand see it. Refuses and logs `;` and newlines. */
    void ExecuteCommand(std::string_view command) const;

    /** Does nothing for a team outside Spectator..CT or when the vtable slot did not bind. */
    void ChangeTeam(VoltMod::Team team) const;

    Status Respawn() const;

private:
    int _slot = -1;
    /** Resolved once: callers ask for the pawn several times per wrapper. */
    CEntityInstance* _pawn = nullptr;
};

}  // namespace VoltMod
