#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief CBaseModelEntity::m_nRenderMode values (legacy Source numbering).
 * Only the ones plugins actually need are listed.
 */
enum class RenderMode_t : uint8_t
{
    Normal = 0,
    TransTexture = 3,
};

/** RGBA white at 100% alpha. Low byte is R, high byte is A in CS2's m_clrRender. */
constexpr uint32_t ColorOpaqueWhite = 0xFFFFFFFFu;

/** RGBA white at 0% alpha - fully invisible. */
constexpr uint32_t ColorInvisible = 0x00FFFFFFu;

/**
 * @brief Set m_nRenderMode and m_clrRender on any CBaseModelEntity (player pawn,
 * weapon, wearable, world prop, dropped weapon, etc.).
 *
 * Offsets come from @p schema, which caches them itself. Safe to call with a null entity (no-op).
 *
 * @param schema Offset source; the caller's own SchemaService (`runtime.Schema()`).
 * @param entity Target. Must derive from CBaseModelEntity.
 * @param mode   Render mode (see RenderMode_t).
 * @param color  RGBA, low byte = R, high byte = A.
 */
void SetRender(SchemaService& schema, CEntityInstance* entity, RenderMode_t mode, uint32_t color);

}  // namespace VoltMod
