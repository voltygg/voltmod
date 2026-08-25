#pragma once

#include <VoltMod/Sdk/Visibility/GlowVision.hpp>
#include <memory>
#include <utility>

namespace VoltMod::Sdk
{

class EntityOpsService;
class TransmitFilterService;

/**
 * @brief Builds per-viewer visibility effects with the entity services already bound.
 *
 * The runtime owns one as @c runtime.Visibility. All three services it holds must outlive
 * it - and the objects it creates, which keep the same references - so the runtime declares
 * them above it.
 */
class VisibilityService
{
public:
    VisibilityService(EntitySystem& entities, EntityOpsService& ops, TransmitFilterService& transmit)
        : _entities(entities), _ops(ops), _transmit(transmit)
    {}
    VisibilityService(const VisibilityService&) = delete;
    VisibilityService& operator=(const VisibilityService&) = delete;

    /** A @ref GlowVision for @p beneficiarySlot. Shared because the usual driver is a repeating
     *  tick that captures it; call @ref GlowVision::Destroy before dropping the last owner. */
    std::shared_ptr<GlowVision> CreateGlow(int beneficiarySlot, GlowVision::Config config = {}) const
    {
        return std::make_shared<GlowVision>(_entities, _ops, _transmit, beneficiarySlot, std::move(config));
    }

private:
    EntitySystem& _entities;
    EntityOpsService& _ops;
    TransmitFilterService& _transmit;
};

}  // namespace VoltMod::Sdk
