#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <VoltMod/Sdk/Entity/MoveType.hpp>
#include <VoltMod/Sdk/Entity/PawnOps.hpp>
#include <cmath>
#include <cstdint>
#include <mathlib/vector.h>
#include <memory>
#include <numbers>
#include <random>

namespace VoltMod::Sdk::PawnOps
{

namespace
{
float Rand(float lo, float hi)
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

/**
 * One slap's fall protection: the pending godmode clear plus the slot listener that cancels it.
 * The timer's callback holds the only strong reference, so both die together the moment the timer
 * fires or is cancelled - including from Scheduler::CancelAll during runtime teardown, which runs
 * while SlotEvents is still alive. The listener holds a weak reference so it can never keep the
 * state (and therefore itself) alive.
 */
struct FallProtection
{
    uint64_t Timer = 0;
    Core::Subscription SlotChanged;
};
}  // namespace

Vector ClearedDestination(const PlayerController& anchor, float clearance)
{
    Vector origin = anchor.GetAbsOrigin();
    float yawRad = anchor.GetEyeAngles().y * std::numbers::pi_v<float> / 180.0f;
    origin.x += std::cos(yawRad) * clearance;
    origin.y += std::sin(yawRad) * clearance;
    return origin;
}

void SwapOrigins(const PlayerController& a, const PlayerController& b)
{
    // Read both before either move, since each origin is the other pawn's destination.
    Vector posA = a.GetAbsOrigin();
    Vector posB = b.GetAbsOrigin();
    Vector zero{0.0f, 0.0f, 0.0f};
    a.Teleport(&posB, nullptr, &zero);
    b.Teleport(&posA, nullptr, &zero);
}

void ShiftZ(const PlayerController& pc, float deltaZ)
{
    Vector origin = pc.GetAbsOrigin();
    origin.z += deltaZ;
    pc.Teleport(&origin, nullptr, nullptr);
}

bool ToggleNoclip(const PlayerController& pc)
{
    bool turningOn = (pc.GetMoveType() != MoveType::NoClip);
    pc.SetMoveType(turningOn ? MoveType::NoClip : MoveType::Walk);
    return turningOn;
}

bool ToggleFreeze(const PlayerController& pc)
{
    bool turningOn = (pc.GetMoveType() != MoveType::None);
    pc.SetMoveType(turningOn ? MoveType::None : MoveType::Walk);
    return turningOn;
}

bool HasGodmode(const PlayerController& pc)
{
    return (pc.GetFlags() & FL_GODMODE) != 0;
}

void SetGodmode(const PlayerController& pc, bool enable)
{
    uint32_t flags = pc.GetFlags();
    pc.SetFlags(enable ? (flags | FL_GODMODE) : (flags & ~FL_GODMODE));
}

bool ToggleGodmode(const PlayerController& pc)
{
    bool turningOn = !HasGodmode(pc);
    SetGodmode(pc, turningOn);
    return turningOn;
}

void Slap(const PlayerController& pc, Core::Scheduler& scheduler, Core::SlotEvents& slots, float upward,
          float horizontal, int fallProtectMs)
{
    // Write velocity directly on the pawn rather than through the Teleport vfunc.
    // Teleport(nullptr origin, ...) was crashing the server in CS2 builds we tested;
    // m_vecAbsVelocity is the conventional path for velocity-only changes.
    pc.SetVelocity({Rand(-horizontal, horizontal), Rand(-horizontal, horizontal), upward});

    // Only toggle godmode for fall protection if the target wasn't already in godmode, otherwise
    // the delayed clear below would silently strip an externally applied godmode.
    if (fallProtectMs <= 0 || HasGodmode(pc))
        return;

    SetGodmode(pc, true);

    const int slot = pc.GetSlot();
    auto& entities = pc.Entities();
    auto state = std::make_shared<FallProtection>();

    // Re-resolve the controller when the timer fires rather than holding this one: the wrapper
    // caches an entity pointer, and the protection window outlives the frame it was taken in.
    state->Timer = scheduler.Delay(fallProtectMs, [state, &entities, slot] {
        PlayerController target = entities.Controller(slot);
        if (target.IsValid())
            SetGodmode(target, false);
    });

    // The player can disconnect inside the protection window; clearing godmode off whoever takes
    // the slot next is not our business, so drop the timer as soon as the seat changes hands.
    state->SlotChanged = slots.Listen([weak = std::weak_ptr<FallProtection>(state), slot, &scheduler](int changed) {
        if (changed != slot)
            return;
        if (auto owned = weak.lock())
            scheduler.Cancel(owned->Timer);
    });
}

bool ChangeTeamSafe(const PlayerController& pc, int team)
{
    if (team < TeamSpectator || team > TeamCT)
        return false;
    pc.ChangeTeam(team);
    return true;
}

}  // namespace VoltMod::Sdk::PawnOps
