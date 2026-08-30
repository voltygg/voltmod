#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <cstdint>

namespace VoltMod::Schema
{

/** Dirty the field at @p offset on @p entity for the next snapshot. */
void NotifyEntity(::CEntityInstance* entity, int32_t offset);

/**
 * Dirty the field at @p offset on a component that embeds `__m_pChainEntity` at @p chainOffset.
 *
 * The engine wants the chainer's path index alongside the offset, and the entity to notify is
 * the one the chainer points at - usually the component's own entity, but not always.
 */
void NotifyThroughChain(void* component, int32_t chainOffset, int32_t offset);

}  // namespace VoltMod::Schema
