#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/PawnOps.hpp>
#include <VoltMod/Entities/PlayerController.hpp>
#include <array>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief Pawn manipulations that need framework services, bound once instead of threaded
 * through every call site.
 *
 * The runtime owns one as @c runtime.Pawns. The plain @ref PawnOps free functions cover
 * everything that needs no service at all.
 */
class Pawns
{
public:
    /** All three must outlive this service; the runtime declares them above it. */
    Pawns(Scheduler& scheduler, SlotEvents& slots, EntitySystem& entities);
    Pawns(const Pawns&) = delete;
    Pawns& operator=(const Pawns&) = delete;

    /** Punt the pawn upward with random jitter, granting FL_GODMODE for @p fallProtectMs so the
     *  landing does not kill it. Pre-existing godmode is left alone, and the clear is dropped if
     *  the seat changes hands, so the next occupant never has godmode stripped. */
    void Slap(const PlayerController& pc, float upward = 800.0f, float horizontal = 100.0f, int fallProtectMs = 3000);

    /** Slay @p slot's pawn after @p delayMs, for effects that want the animation to play first.
     *  Re-resolves the pawn when it fires, replaces any slay already pending for the slot, and
     *  drops it if the seat changes hands - so it can never kill the next occupant. */
    void SlayDelayed(int slot, int64_t delayMs);

private:
    Scheduler& _scheduler;
    EntitySystem& _entities;
    /** Pending fall-protection clear per slot; dropping the entry cancels it. */
    std::array<Subscription, MaxPlayers> _fallProtect{};
    /** Pending delayed slay per slot; dropping the entry cancels it. */
    std::array<Subscription, MaxPlayers> _slay{};
    /** Declared last: drops before the timers its callback cancels. */
    Subscription _slotListener;
};

}  // namespace VoltMod
