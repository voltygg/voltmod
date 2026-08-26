#pragma once

#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Hooks/GlowVision.hpp>
#include <VoltMod/Hooks/Transmit.hpp>
#include <memory>
#include <utility>

namespace VoltMod
{

/**
 * @brief Builds per-viewer visibility effects with the entity services already bound.
 *
 * The runtime owns one as @c runtime.Visibility. All three services it holds must outlive
 * it - and the objects it creates, which keep the same references - so the runtime declares
 * them above it.
 */
class Visibility
{
public:
    Visibility(EntitySystem& entities, EntityOps& ops, Transmit& transmit)
        : _entities(entities), _ops(ops), _transmit(transmit)
    {}
    Visibility(const Visibility&) = delete;
    Visibility& operator=(const Visibility&) = delete;

    /** A @ref GlowVision for @p beneficiarySlot. Shared because the usual driver is a repeating
     *  tick that captures it; call @ref GlowVision::Destroy before dropping the last owner. */
    std::shared_ptr<GlowVision> CreateGlow(int beneficiarySlot, GlowConfig config = {}) const
    {
        return std::make_shared<GlowVision>(_entities, _ops, _transmit, beneficiarySlot, std::move(config));
    }

private:
    EntitySystem& _entities;
    EntityOps& _ops;
    Transmit& _transmit;
};

}  // namespace VoltMod
