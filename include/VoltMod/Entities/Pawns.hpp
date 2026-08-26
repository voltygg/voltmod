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

private:
    Scheduler& _scheduler;
    EntitySystem& _entities;
    /** Pending fall-protection clear per slot, 0 when none. */
    std::array<uint64_t, MaxPlayers> _fallProtect{};
    /** Declared last: drops before the handles its callback touches. */
    Subscription _slotListener;
};

}  // namespace VoltMod
