#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/Controller.hpp>

namespace VoltMod
{

// Engine team indices.
inline constexpr int TeamNone = 0;
inline constexpr int TeamSpectator = 1;
inline constexpr int TeamT = 2;
inline constexpr int TeamCT = 3;

/**
 * @brief Pawn manipulations composed from @ref Pawn primitives: teleport destinations,
 * movement/godmode toggles, burying. Free functions so the wrappers stay field access plus the
 * engine verbs. Anything needing framework services lives on @ref Pawns instead.
 */
namespace PawnOps
{

/** Origin `clearance` units ahead of `anchor` along its eye yaw; Z stays at the anchor's level.
 *  The default clears the ~32-unit player hull so a teleported player doesn't clip into the
 *  anchor and stick (both frozen until one dies). */
Vector ClearedDestination(const Pawn& anchor, float clearance = 48.0f);

/** Exchange the two pawns' exact origins, zeroing both velocities. Both spots are vacated in
 *  the same frame, so no clearance offset is needed - and offsetting along facings can converge
 *  the two destinations into a collision stick when the players face each other. */
void SwapOrigins(const Pawn& a, const Pawn& b);

/** Teleport the pawn `deltaZ` units vertically (negative buries, positive unburies). */
void ShiftZ(const Pawn& pawn, float deltaZ);

/** Toggle noclip <-> walk. Returns the new on-state. */
bool ToggleNoclip(const Pawn& pawn);

/** Toggle frozen (MoveType None) <-> walk. Returns the new on-state. */
bool ToggleFreeze(const Pawn& pawn);

/** FL_GODMODE flag helpers - the m_fFlags bit is the working CS2 invincibility path
 *  (the legacy m_takedamage write is a no-op). ToggleGodmode returns the new on-state. */
bool HasGodmode(const Pawn& pawn);
void SetGodmode(const Pawn& pawn, bool enable);
bool ToggleGodmode(const Pawn& pawn);

/** ChangeTeam bounds-checked to TeamSpectator..TeamCT. Returns false for out-of-range values. */
bool ChangeTeamSafe(const Controller& controller, int team);

}  // namespace PawnOps

}  // namespace VoltMod
