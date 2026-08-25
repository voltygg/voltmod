#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <VoltMod/Sdk/Entity/MoveType.hpp>
#include <VoltMod/Sdk/Entity/PawnOps.hpp>
#include <VoltMod/Sdk/Entity/PawnService.hpp>
#include <cmath>
#include <cstdint>
#include <mathlib/vector.h>
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

bool ChangeTeamSafe(const PlayerController& pc, int team)
{
    if (team < TeamSpectator || team > TeamCT)
        return false;
    pc.ChangeTeam(team);
    return true;
}

}  // namespace VoltMod::Sdk::PawnOps

namespace VoltMod::Sdk
{

PawnService::PawnService(Core::Scheduler& scheduler, Core::SlotEvents& slots, EntitySystem& entities)
    : _scheduler(scheduler),
      _entities(entities),
      // SlotEvents also fires when a slot is filled; either edge means the pending clear no longer
      // belongs to whoever sits there, so both drop it.
      _slotListener(slots.Listen([this](int slot) {
          if (!Core::IsValidSlot(slot))
              return;
          _scheduler.Cancel(_fallProtect[slot]);
          _fallProtect[slot] = 0;
      }))
{}

void PawnService::Slap(const PlayerController& pc, float upward, float horizontal, int fallProtectMs)
{
    // Write velocity directly on the pawn rather than through the Teleport vfunc.
    // Teleport(nullptr origin, ...) was crashing the server in CS2 builds we tested;
    // m_vecAbsVelocity is the conventional path for velocity-only changes.
    pc.SetVelocity({PawnOps::Rand(-horizontal, horizontal), PawnOps::Rand(-horizontal, horizontal), upward});

    // Only toggle godmode for fall protection if the target wasn't already in godmode, otherwise
    // the delayed clear below would silently strip an externally applied godmode.
    const int slot = pc.GetSlot();
    if (fallProtectMs <= 0 || !Core::IsValidSlot(slot) || PawnOps::HasGodmode(pc))
        return;

    PawnOps::SetGodmode(pc, true);
    _scheduler.Cancel(_fallProtect[slot]);

    // Re-resolve the controller when the timer fires rather than holding this one: the wrapper
    // caches an entity pointer, and the protection window outlives the frame it was taken in.
    // The Runtime destroys the Scheduler after this service, discarding pending timers unrun, so
    // the captured `this` is never dereferenced after it dies.
    _fallProtect[slot] = _scheduler.Delay(fallProtectMs, [this, slot] {
        _fallProtect[slot] = 0;
        PlayerController target = _entities.Controller(slot);
        if (target.IsValid())
            PawnOps::SetGodmode(target, false);
    });
}

}  // namespace VoltMod::Sdk
