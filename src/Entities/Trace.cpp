#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/Trace.hpp>
#include <bspflags.h>
#include <const.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <gametrace.h>
#include <ray.h>
#include <utility>

namespace VoltMod
{

/** Windows are see-through and clips are invisible, so neither hides a player. */
static constexpr uint64_t SightMask = CONTENTS_SOLID | CONTENTS_BLOCK_LOS;

// No entity iteration: the engine never calls back into this module through the filter.
static CTraceFilter MakeFilter(const TraceOptions& options)
{
    CTraceFilter filter(options.Layers == TraceLayers::Sight ? SightMask : MASK_PLAYERSOLID, COLLISION_GROUP_DEFAULT,
                        false);
    filter.SetPassEntity1(options.Ignore1);
    filter.SetPassEntity2(options.Ignore2);
    return filter;
}

static EntityRef RefOf(const CEntityInstance* entity)
{
    if (!entity || !entity->m_pEntity)
    {
        return {};
    }
    return {static_cast<uint32_t>(entity->GetRefEHandle().ToInt())};
}

static TraceHit ToHit(const CGameTrace& trace)
{
    return TraceHit{.Hit = trace.DidHit(),
                    .Fraction = trace.m_flFraction,
                    .End = trace.m_vEndPos,
                    .Normal = trace.m_vHitNormal,
                    .HitEntity = RefOf(trace.m_pEnt)};
}

// The interface carries no state, so its own class table stands in for the object.
static EngineNavPhysics* NavPhysics(void*& table)
{
    return reinterpret_cast<EngineNavPhysics*>(&table);
}

Status Trace::Available() const
{
    if (!_bindings.NavTraceLine)
    {
        return std::unexpected(Error::Unsupported("the Nav_TraceLine vtable slot did not bind"));
    }
    return {};
}

Result<TraceHit> Trace::Line(const Vector& from, const Vector& to, const TraceOptions& options) const
{
    if (Status available = Available(); !available)
    {
        return std::unexpected(available.error());
    }

    CTraceFilter filter = MakeFilter(options);
    void* table = _bindings.NavTraceLine.Table();
    // The Linux engine writes the trace with aligned SIMD stores.
    alignas(16) CGameTrace trace;
    _bindings.NavTraceLine(NavPhysics(table), &from, &to, &filter, &trace);
    return ToHit(trace);
}

Result<TraceHit> Trace::Box(const Vector& from, const Vector& to, const Vector& mins, const Vector& maxs,
                            const TraceOptions& options) const
{
    if (!_bindings.NavTraceShape)
    {
        return std::unexpected(Error::Unsupported("the Nav_TraceShape vtable slot did not bind"));
    }

    const Ray_t ray(mins, maxs);
    CTraceFilter filter = MakeFilter(options);
    void* table = _bindings.NavTraceShape.Table();
    alignas(16) CGameTrace trace;
    _bindings.NavTraceShape(NavPhysics(table), &ray, &from, &to, &filter, &trace);
    return ToHit(trace);
}

Result<bool> Trace::Clear(const Vector& from, const Vector& to, const TraceOptions& options) const
{
    Result<TraceHit> hit = Line(from, to, options);
    if (!hit)
    {
        return std::unexpected(std::move(hit).error());
    }
    return !hit->Hit;
}

}  // namespace VoltMod
