#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Sdk/Entity/PawnOps.hpp>
#include <VoltMod/Sdk/Entity/PlayerController.hpp>

namespace VoltMod::Sdk
{

/**
 * @brief Pawn manipulations that need framework services, bound once instead of threaded
 * through every call site.
 *
 * The runtime owns one as @c runtime.Pawns; it is the normal entry point for these
 * operations. The plain @ref PawnOps free functions stay available for code that already
 * holds the services or wants the primitive.
 */
class PawnService
{
public:
    /** @p scheduler and @p slots must outlive this service; the runtime declares both above it. */
    PawnService(Core::Scheduler& scheduler, Core::SlotEvents& slots) : _scheduler(scheduler), _slots(slots) {}
    PawnService(const PawnService&) = delete;
    PawnService& operator=(const PawnService&) = delete;

    /** Punt the pawn upward with random horizontal jitter, granting FL_GODMODE for
     *  @p fallProtectMs so the landing does not kill the target. See @ref PawnOps::Slap. */
    void Slap(const PlayerController& pc, float upward = 800.0f, float horizontal = 100.0f,
              int fallProtectMs = 3000) const
    {
        PawnOps::Slap(pc, _scheduler, _slots, upward, horizontal, fallProtectMs);
    }

private:
    Core::Scheduler& _scheduler;
    Core::SlotEvents& _slots;
};

}  // namespace VoltMod::Sdk
