#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <mathlib/vector.h>

namespace VoltMod
{

/** Which interaction layers a trace stops at. */
enum class TraceLayers
{
    Sight,  ///< world geometry and line-of-sight blockers: what hides one player from another
    Solid,  ///< everything a player body collides with, including other players
};

struct TraceOptions
{
    TraceLayers Layers = TraceLayers::Sight;
    /** Entities the trace passes through, such as the two pawns whose sight line is being asked. */
    CEntityInstance* Ignore1 = nullptr;
    CEntityInstance* Ignore2 = nullptr;
};

/** Where a line trace stopped. */
struct TraceHit
{
    bool Hit = false;       ///< something lay on the line, or the start point was inside a solid
    float Fraction = 1.0f;  ///< share of the line travelled before the hit; 1 when nothing was hit
    Vector End;             ///< the hit point, or the requested end when nothing was hit
};

/**
 * @brief Line traces through the engine's physics query.
 *
 * The engine keeps its query object private, so the service captures it from the engine's own
 * first trace after each map start: a short-lived hook on the bound TraceShape function records
 * the receiver and removes itself a tick later. Until that first engine trace, @ref Available
 * reports NotReady and every trace returns that error rather than guessing.
 *
 * @code
 * const auto clear = runtime.Hooks.Trace.Clear(eye, target, {.Ignore1 = self.Raw(), .Ignore2 = other.Raw()});
 * if (clear && *clear)
 *     ...  // nothing solid between the two points
 * @endcode
 */
class Trace
{
public:
    /** @p bindings supplies TraceShape and @p scheduler releases the capture hook. Both outlive it. */
    Trace(const Bindings& bindings, Scheduler& scheduler);
    Trace(const Trace&) = delete;
    Trace& operator=(const Trace&) = delete;

    /** Arm the capture hook. Runtime::Start calls this once; a failure names the missing binding. */
    Status Initialize();

    /** Re-arm the capture: the map rebuilds the physics world, so the query is taken again. */
    void OnServerStartup();

    /** Unsupported when TraceShape did not bind; NotReady until the engine has traced once. */
    Status Available() const;

    /** Trace a line from @p from to @p to. */
    Result<TraceHit> Line(const Vector& from, const Vector& to, const TraceOptions& options = {}) const;

    /** True when nothing in the chosen layers lies between the two points. */
    Result<bool> Clear(const Vector& from, const Vector& to, const TraceOptions& options = {}) const;

private:
    Status Arm();

    const Bindings& _bindings;
    Scheduler& _scheduler;
    EnginePhysicsQuery* _query = nullptr;
    Subscription _capture;
    Subscription _release;
};

}  // namespace VoltMod
