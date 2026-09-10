#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/MovementFreeze.hpp>

namespace VoltMod
{

/**
 * @brief Holding players still while a menu is open, for every menu surface.
 *
 * One per server, on the @ref Runtime: the setting is server-wide, so a player is treated the
 * same way whichever surface drew their menu. A surface says only when a session opens and
 * closes; honouring the setting and re-applying the hold across a respawn happen here.
 *
 * Costs nothing per frame while nobody is held.
 */
class MenuFreeze
{
public:
    /** All three must outlive this instance. @p slots drops a hold when a slot changes hands. */
    MenuFreeze(EntitySystem& entities, Scheduler& scheduler, SlotEvents& slots);

    /**
     * Freeze movement for the duration of a session, so navigating does not also walk the player
     * around. The original MoveType is restored when the session closes. Off by default; turning
     * it off also releases whoever the previous setting had already frozen.
     */
    void Enable(bool enabled);

    [[nodiscard]] bool Enabled() const noexcept { return _enabled; }

    /** A session opened for @p slot. @p requested is that session's @ref MenuOptions::FreezeMovement,
     *  so a menu players reach mid-round can opt out while the setting stays on. */
    void Open(int slot, bool requested);

    /** The session closed: give the pawn its movement back. */
    void Close(int slot);

private:
    /** Re-apply every hold, which is what carries one across a death and respawn. */
    void OnGameFrame();

    /** Start or stop the per-frame work to match what is currently held. */
    void SyncFrameWork();

    /** Whether this session asked to be held still, and the pawn it is holding. */
    struct State
    {
        bool Requested = false;
        MovementFreeze Movement;
    };

    EntitySystem& _entities;
    Scheduler& _scheduler;
    bool _enabled = false;
    PerSlot<State> _states;
    /** Declared last: per-frame delivery drops before the state it touches. */
    Subscription _onFrame;
};

}  // namespace VoltMod
