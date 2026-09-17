#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/Trace.hpp>
#include <bspflags.h>
#include <const.h>
#include <gametrace.h>
#include <utility>

namespace VoltMod
{

/** Windows are see-through and clips are invisible, so neither hides a player. */
static constexpr uint64_t SightMask = CONTENTS_SOLID | CONTENTS_BLOCK_LOS;

Status Trace::Available() const
{
    if (!_bindings.NavTraceLine)
        return std::unexpected(Error::Unsupported("the Nav_TraceLine vtable slot did not bind"));
    return {};
}

Result<TraceHit> Trace::Line(const Vector& from, const Vector& to, const TraceOptions& options) const
{
    if (Status available = Available(); !available)
        return std::unexpected(available.error());

    const uint64_t mask = options.Layers == TraceLayers::Sight ? SightMask : MASK_PLAYERSOLID;

    // No entity iteration: the engine never calls back into this module through the filter.
    CTraceFilter filter(mask, COLLISION_GROUP_DEFAULT, false);
    filter.SetPassEntity1(options.Ignore1);
    filter.SetPassEntity2(options.Ignore2);

    // The interface carries no state, so its own class table stands in for the object.
    void* table = _bindings.NavTraceLine.Table();
    auto* nav = reinterpret_cast<EngineNavPhysics*>(&table);

    CGameTrace trace;
    _bindings.NavTraceLine(nav, &from, &to, &filter, &trace);

    return TraceHit{.Hit = trace.DidHit(), .Fraction = trace.m_flFraction, .End = trace.m_vEndPos};
}

Result<bool> Trace::Clear(const Vector& from, const Vector& to, const TraceOptions& options) const
{
    Result<TraceHit> hit = Line(from, to, options);
    if (!hit)
        return std::unexpected(std::move(hit).error());
    return !hit->Hit;
}

}  // namespace VoltMod
