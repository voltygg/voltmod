#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Team.hpp>
#include <VoltMod/Entities/Controller.hpp>

namespace VoltMod
{

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

/** Toggle frozen (MOVETYPE_NONE) <-> walk. Returns the new on-state. */
bool ToggleFreeze(const Pawn& pawn);

/** Returns the new on-state. */
bool ToggleGodmode(const Pawn& pawn);

/** @ref Controller::ChangeTeam, as a bool. */
bool ChangeTeamSafe(const Controller& controller, Team team);

}  // namespace PawnOps

}  // namespace VoltMod
