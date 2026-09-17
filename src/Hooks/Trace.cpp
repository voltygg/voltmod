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

Trace::Trace(const Bindings& bindings, Scheduler& scheduler) : _bindings(bindings), _scheduler(scheduler) {}

Status Trace::Initialize()
{
    if (_capture)
        return {};

    auto hook = HookFunction(
        "Trace query capture", _bindings.TraceShape,
        [this](EnginePhysicsQuery& query, const void*, const Vector*, const Vector*, void*, void*) {
            if (_query)
                return;  // captured already; the removal below is still pending

            _query = &query;
            // Removing a hook from inside its own call is unsafe, so let the tick finish first.
            _release = _scheduler.NextTick([this] { _capture.Reset(); });
        });
    if (!hook)
        return std::unexpected(hook.error());

    _capture = std::move(*hook);
    return {};
}

void Trace::OnServerStartup()
{
    _query = nullptr;
    // Drop a pending removal first, or it would tear down the capture re-armed below.
    _release.Reset();
    (void)Initialize();
}

Status Trace::Available() const
{
    if (_query)
        return {};
    if (!_bindings.TraceShape)
        return std::unexpected(Error::Unsupported("the TraceShape signature did not bind"));
    return std::unexpected(Error::NotReady("the engine has not traced yet this map"));
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

    Ray_t ray;
    CGameTrace trace;
    _bindings.TraceShape(_query, &ray, &from, &to, &filter, &trace);

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
