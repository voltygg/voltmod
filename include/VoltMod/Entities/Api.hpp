#pragma once

// The Entities module's public surface in one include: frame-local entity wrappers, the
// resolution and manipulation services built on them, and the convar type they read
// settings through. Include the individual headers when a translation unit only needs a
// few of these.

#include <VoltMod/Engine/ConVarLease.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Entities/Controller.hpp>
#include <VoltMod/Entities/EffectOps.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/Field.hpp>
#include <VoltMod/Entities/HitGroup.hpp>
#include <VoltMod/Entities/Items.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Entities/MoveType.hpp>
#include <VoltMod/Entities/ObserverMode.hpp>
#include <VoltMod/Entities/Pawn.hpp>
#include <VoltMod/Entities/PawnOps.hpp>
#include <VoltMod/Entities/PawnPredicates.hpp>
#include <VoltMod/Entities/Pawns.hpp>
#include <VoltMod/Entities/Render.hpp>
