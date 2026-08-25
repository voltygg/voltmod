#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Sdk/Entity/PlayerController.hpp>

class Vector;

namespace VoltMod::Sdk
{

// Engine team indices.
inline constexpr int TeamNone = 0;
inline constexpr int TeamSpectator = 1;
inline constexpr int TeamT = 2;
inline constexpr int TeamCT = 3;

/**
 * @brief Common pawn manipulations composed from PlayerController primitives: teleport
 * destinations, movement/godmode toggles, and gameplay staples like slap and bury.
 * Free functions so PlayerController itself stays a thin field accessor.
 */
namespace PawnOps
{

/** Origin `clearance` units ahead of `anchor` along its eye yaw; Z stays at the anchor's level.
 *  The default clears the ~32-unit player hull so a teleported player doesn't clip into the
 *  anchor and stick (both frozen until one dies). */
Vector ClearedDestination(const PlayerController& anchor, float clearance = 48.0f);

/** Exchange the two pawns' exact origins, zeroing both velocities. Both spots are vacated in
 *  the same frame, so no clearance offset is needed - and offsetting along facings can converge
 *  the two destinations into a collision stick when the players face each other. */
void SwapOrigins(const PlayerController& a, const PlayerController& b);

/** Teleport the pawn `deltaZ` units vertically (negative buries, positive unburies). */
void ShiftZ(const PlayerController& pc, float deltaZ);

/** Toggle noclip <-> walk. Returns the new on-state. */
bool ToggleNoclip(const PlayerController& pc);

/** Toggle frozen (MoveType None) <-> walk. Returns the new on-state. */
bool ToggleFreeze(const PlayerController& pc);

/** FL_GODMODE flag helpers - the m_fFlags bit is the working CS2 invincibility path
 *  (the legacy m_takedamage write is a no-op). ToggleGodmode returns the new on-state. */
bool HasGodmode(const PlayerController& pc);
void SetGodmode(const PlayerController& pc, bool enable);
bool ToggleGodmode(const PlayerController& pc);

/**
 * Punt the pawn upward with random horizontal jitter, granting FL_GODMODE for `fallProtectMs`
 * so the landing doesn't kill the target. Pre-existing godmode is left untouched.
 *
 * @param scheduler runs the delayed godmode clear; pass `runtime.Scheduler`.
 * @param slots     cancels that clear if the seat changes hands first, so the next occupant of
 *                  @p pc's slot never has godmode stripped; pass `runtime.Slots`.
 * Both must outlive the protection window - the Runtime owns them and cancels its scheduler
 * before they go away.
 */
void Slap(const PlayerController& pc, Core::Scheduler& scheduler, Core::SlotEvents& slots, float upward = 800.0f,
          float horizontal = 100.0f, int fallProtectMs = 3000);

/** ChangeTeam bounds-checked to TeamSpectator..TeamCT. Returns false for out-of-range values. */
bool ChangeTeamSafe(const PlayerController& pc, int team);

}  // namespace PawnOps

}  // namespace VoltMod::Sdk
