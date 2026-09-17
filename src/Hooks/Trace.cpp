#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Hooks/Trace.hpp>
#include <VoltMod/Unsafe/Hook.hpp>
#include <bspflags.h>
#include <const.h>
#include <gametrace.h>
#include <ray.h>
#include <utility>

namespace VoltMod
{

/** Windows are see-through and clips are invisible, so neither hides a player. */
static constexpr uint64_t SightMask = CONTENTS_SOLID | CONTENTS_BLOCK_LOS;
static constexpr uint64_t SolidMask = MASK_PLAYERSOLID;

static uint64_t MaskOf(TraceLayers layers)
{
    return layers == TraceLayers::Sight ? SightMask : SolidMask;
}

Trace::Trace(const Bindings& bindings, Scheduler& scheduler) : _bindings(bindings), _scheduler(scheduler) {}

Status Trace::Initialize()
{
    return Arm();
}

void Trace::OnServerStartup()
{
    _query = nullptr;
    _release.Reset();
    if (Status armed = Arm(); !armed)
        Log::Warn("Trace: {}; traces are unavailable this map.", armed.error().Detail);
}

Status Trace::Arm()
{
    if (_capture)
        return {};

    auto hook = HookFunction(
        "Trace query capture", _bindings.TraceShape,
        [this](EnginePhysicsQuery& query, const void*, const Vector*, const Vector*, void*, void*) {
            _query = &query;
            // Removing a hook from inside its own call is unsafe, so let the tick finish first.
            _release = _scheduler.NextTick([this] { _capture.Reset(); });
        });
    if (!hook)
        return std::unexpected(Error::Unsupported(hook.error().Detail));

    _capture = std::move(*hook);
    return {};
}

Status Trace::Available() const
{
    if (!_bindings.TraceShape)
        return std::unexpected(Error::Unsupported("the TraceShape signature did not bind"));
    if (!_query)
        return std::unexpected(Error::NotReady("the engine has not traced yet this map"));
    return {};
}

Result<TraceHit> Trace::Line(const Vector& from, const Vector& to, const TraceOptions& options) const
{
    if (Status available = Available(); !available)
        return std::unexpected(available.error());

    // No entity iteration: the engine never calls back into this module through the filter.
    CTraceFilter filter(MaskOf(options.Layers), COLLISION_GROUP_DEFAULT, false);
    filter.SetPassEntity1(options.Ignore1);
    filter.SetPassEntity2(options.Ignore2);

    Ray_t ray;
    CGameTrace trace;
    _bindings.TraceShape(_query, &ray, &from, &to, &filter, &trace);

    return TraceHit{.Hit = trace.DidHit(), .Fraction = trace.m_flFraction, .End = trace.m_vEndPos};
}

Result<bool> Trace::Clear(const Vector& from, const Vector& to, const TraceOptions& options) const
{
    const Result<TraceHit> hit = Line(from, to, options);
    if (!hit)
        return std::unexpected(hit.error());
    return !hit->Hit;
}

}  // namespace VoltMod
