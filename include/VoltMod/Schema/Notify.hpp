#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <cstdint>

namespace VoltMod::Schema
{

/** Dirty the field at @p offset on @p entity for the next snapshot. */
void NotifyEntity(::CEntityInstance* entity, int32_t offset);

/**
 * Dirty the field at @p offset on a component whose owner link (the engine's `__m_pChainEntity`)
 * sits at @p ownerLinkOffset.
 *
 * The engine wants the link's path index alongside the offset, and the entity to notify is the
 * one the link points at - usually the component's own entity, but not always.
 */
void NotifyComponentOwner(void* component, int32_t ownerLinkOffset, int32_t offset);

/** The entity that owns @p component, read through its owner link; nullptr when unset. */
::CEntityInstance* ComponentOwner(const void* component, int32_t ownerLinkOffset);

}  // namespace VoltMod::Schema
