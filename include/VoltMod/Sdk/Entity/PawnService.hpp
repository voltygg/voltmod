#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Sdk/Entity/PawnOps.hpp>
#include <VoltMod/Sdk/Entity/PlayerController.hpp>
#include <array>
#include <cstdint>

namespace VoltMod::Sdk
{

class EntitySystem;

/**
 * @brief Pawn manipulations that need framework services, bound once instead of threaded
 * through every call site.
 *
 * The runtime owns one as @c runtime.Pawns; it is the entry point for these operations. The
 * plain @ref PawnOps free functions cover everything that needs no service at all.
 */
class PawnService
{
public:
    /** @p scheduler, @p slots and @p entities must outlive this service; the runtime declares
     *  all three above it. */
    PawnService(Core::Scheduler& scheduler, Core::SlotEvents& slots, EntitySystem& entities);
    PawnService(const PawnService&) = delete;
    PawnService& operator=(const PawnService&) = delete;

    /**
     * Punt the pawn upward with random horizontal jitter, granting FL_GODMODE for
     * @p fallProtectMs so the landing does not kill the target. Pre-existing godmode is left
     * untouched, and the clear is dropped if the seat changes hands first, so the next occupant
     * of the slot never has godmode stripped.
     */
    void Slap(const PlayerController& pc, float upward = 800.0f, float horizontal = 100.0f, int fallProtectMs = 3000);

private:
    Core::Scheduler& _scheduler;
    EntitySystem& _entities;
    /** Pending fall-protection clear per slot, 0 when none. One listener guards the whole table,
     *  so a slap allocates nothing beyond its timer. */
    std::array<uint64_t, Core::MaxPlayers> _fallProtect{};
    /** Declared last: it drops before the handles its callback touches. */
    Core::Subscription _slotListener;
};

}  // namespace VoltMod::Sdk
