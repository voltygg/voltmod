#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/MoveType.hpp>
#include <VoltMod/Entities/PawnOps.hpp>
#include <VoltMod/Entities/Pawns.hpp>
#include <cmath>
#include <cstdint>
#include <mathlib/vector.h>
#include <numbers>
#include <random>

namespace VoltMod::PawnOps
{

static float Rand(float lo, float hi)
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

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

}  // namespace VoltMod::PawnOps

namespace VoltMod
{

Pawns::Pawns(Scheduler& scheduler, SlotEvents& slots, EntitySystem& entities)
    : _scheduler(scheduler),
      _entities(entities),
      // Either edge of a slot change means the pending clear no longer belongs to whoever sits there.
      _slotListener(slots.Changed += [this](int slot) {
          if (!IsValidSlot(slot))
              return;
          _fallProtect[slot].Reset();
          _slay[slot].Reset();
      })
{}

void Pawns::Slap(const PlayerController& pc, float upward, float horizontal, int fallProtectMs)
{
    // Write velocity directly on the pawn rather than through the Teleport vfunc.
    // Teleport(nullptr origin, ...) was crashing the server in CS2 builds we tested;
    // m_vecAbsVelocity is the conventional path for velocity-only changes.
    pc.SetVelocity({PawnOps::Rand(-horizontal, horizontal), PawnOps::Rand(-horizontal, horizontal), upward});

    // Only toggle godmode for fall protection if the target wasn't already in godmode, otherwise
    // the delayed clear below would silently strip an externally applied godmode.
    const int slot = pc.GetSlot();
    if (fallProtectMs <= 0 || !IsValidSlot(slot) || PawnOps::HasGodmode(pc))
        return;

    PawnOps::SetGodmode(pc, true);

    // Re-resolve rather than holding this controller: it caches an entity pointer and the window
    // outlives the frame. The Runtime discards pending timers unrun, so `this` never dangles.
    // Assigning cancels whatever clear was already pending for this slot.
    _fallProtect[slot] = _scheduler.Delay(fallProtectMs, [this, slot] {
        PlayerController target = _entities.Controller(slot);
        if (target.IsValid())
            PawnOps::SetGodmode(target, false);
    });
}

void Pawns::SlayDelayed(int slot, int64_t delayMs)
{
    if (!IsValidSlot(slot))
        return;

    // Re-resolved on fire for the same reason Slap's clear is, and assigning cancels whatever
    // slay was already pending for this slot.
    _slay[slot] = _scheduler.Delay(delayMs, [this, slot] {
        PlayerController target = _entities.Controller(slot);
        if (target.IsValid() && target.IsAlive())
            target.Slay();
    });
}

}  // namespace VoltMod
