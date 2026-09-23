#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/PawnOps.hpp>
#include <VoltMod/Entities/Pawns.hpp>
#include <cstdint>
#include <mathlib/vector.h>
#include <random>
#include <shareddefs.h>

namespace VoltMod::PawnOps
{

static float Rand(float lo, float hi)
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

Vector ClearedDestination(const Pawn& anchor, float clearance)
{
    // Level the aim so the offset stays horizontal.
    return anchor.Origin() + AngleToForward(QAngle(0.0f, anchor.EyeAngles().y, 0.0f)) * clearance;
}

void SwapOrigins(const Pawn& a, const Pawn& b)
{
    // Read both before either move, since each origin is the other pawn's destination.
    Vector posA = a.Origin();
    Vector posB = b.Origin();
    Vector zero{0.0f, 0.0f, 0.0f};
    (void)a.Teleport(posB, std::nullopt, zero);
    (void)b.Teleport(posA, std::nullopt, zero);
}

void ShiftZ(const Pawn& pawn, float deltaZ)
{
    Vector origin = pawn.Origin();
    origin.z += deltaZ;
    (void)pawn.Teleport(origin, std::nullopt, std::nullopt);
}

bool ToggleNoclip(const Pawn& pawn)
{
    bool turningOn = (pawn.MoveType() != Schema::MoveType_t::MOVETYPE_NOCLIP);
    pawn.SetMoveType(turningOn ? Schema::MoveType_t::MOVETYPE_NOCLIP : Schema::MoveType_t::MOVETYPE_WALK);
    return turningOn;
}

bool ToggleFreeze(const Pawn& pawn)
{
    bool turningOn = (pawn.MoveType() != Schema::MoveType_t::MOVETYPE_NONE);
    pawn.SetMoveType(turningOn ? Schema::MoveType_t::MOVETYPE_NONE : Schema::MoveType_t::MOVETYPE_WALK);
    return turningOn;
}

bool ToggleGodmode(const Pawn& pawn)
{
    bool turningOn = !pawn.Godmode();
    pawn.SetGodmode(turningOn);
    return turningOn;
}

bool ChangeTeamSafe(const Controller& controller, Team team)
{
    return controller.ChangeTeam(team).has_value();
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
          {
              return;
          }
          _fallProtect[slot].Reset();
          _slay[slot].Reset();
      })
{}

void Pawns::Slap(const Pawn& pawn, float upward, float horizontal, int fallProtectMs)
{
    // Write velocity directly on the pawn rather than through the Teleport vfunc.
    // Teleport(nullptr origin, ...) was crashing the server in CS2 builds we tested;
    // m_vecAbsVelocity is the conventional path for velocity-only changes.
    pawn.SetVelocity(Vector{PawnOps::Rand(-horizontal, horizontal), PawnOps::Rand(-horizontal, horizontal), upward});

    // Only toggle godmode for fall protection if the target wasn't already in godmode, otherwise
    // the delayed clear below would silently strip an externally applied godmode.
    const int slot = pawn.Slot();
    if (fallProtectMs <= 0 || !IsValidSlot(slot) || pawn.Godmode())
    {
        return;
    }

    pawn.SetGodmode(true);

    // Re-resolve rather than holding this pawn: it is a frame-local value and the window outlives
    // the frame. The Runtime discards pending timers unrun, so `this` never dangles. Assigning
    // cancels whatever clear was already pending for this slot.
    _fallProtect[slot] = _scheduler.Delay(fallProtectMs, [this, slot] {
        Pawn target = _entities.Pawn(slot);
        if (target)
        {
            target.SetGodmode(false);
        }
    });
}

void Pawns::SlayDelayed(int slot, int64_t delayMs)
{
    if (!IsValidSlot(slot))
    {
        return;
    }

    // Re-resolved on fire for the same reason Slap's clear is, and assigning cancels whatever
    // slay was already pending for this slot.
    _slay[slot] = _scheduler.Delay(delayMs, [this, slot] {
        Pawn target = _entities.Pawn(slot);
        if (target && target.IsAlive())
        {
            (void)target.Slay();
        }
    });
}

}  // namespace VoltMod
