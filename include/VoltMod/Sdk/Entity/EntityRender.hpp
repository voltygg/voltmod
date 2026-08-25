#pragma once

#include <cstdint>

class CEntityInstance;

namespace VoltMod::Sdk
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
 * Resolves the schema offsets via SchemaService on first call and caches them.
 * Safe to call with a null entity (no-op).
 *
 * @param entity Target. Must derive from CBaseModelEntity.
 * @param mode   Render mode (see RenderMode_t).
 * @param color  RGBA, low byte = R, high byte = A.
 */
void SetEntityRender(CEntityInstance* entity, RenderMode_t mode, uint32_t color);

}  // namespace VoltMod::Sdk
