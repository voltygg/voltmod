#pragma once

#include <VoltMod/Engine/Color.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Schema/Generated/Enums.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief Set m_nRenderMode and m_clrRender on any CBaseModelEntity (player pawn,
 * weapon, wearable, world prop, dropped weapon, etc.).
 *
 * A free function because it applies to entities with no wrapper of their own; a player pawn has
 * @ref Pawn::SetRender. Both writes dirty for replication. Safe to call with a null entity (no-op).
 *
 * @param entity Target. Must derive from CBaseModelEntity.
 * @param mode   `kRenderTransAlpha` for a color whose alpha should show, `kRenderNone` to hide.
 */
void SetRender(CEntityInstance* entity, Schema::RenderMode_t mode, Color color);

}  // namespace VoltMod
