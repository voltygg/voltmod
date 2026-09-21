#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
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

/** Where a trace stopped. */
struct TraceHit
{
    bool Hit = false;       ///< something lay on the path, or the start point was inside a solid
    float Fraction = 1.0f;  ///< share of the path travelled before the hit; 1 when nothing was hit
    Vector End;             ///< where the trace stopped: the requested end when nothing was hit
    Vector Normal;          ///< surface normal at the hit; zero when nothing was hit
    EntityRef HitEntity;    ///< what was hit, the world included; empty when nothing was hit
};

/**
 * @brief Line and box traces through the nav mesh's window onto the physics world.
 *
 * CNavPhysicsInterface holds no state of its own, so the call goes through its class vtable with
 * the table itself standing in for the object - the same stand-in @ref HookVirtual uses. Nothing
 * to install and nothing to re-take per map; a trace works as soon as the slot binds.
 *
 * Game-thread only.
 *
 * @code
 * const auto clear = runtime.World.Trace.Clear(eye, target, {.Ignore1 = self.Raw(), .Ignore2 = other.Raw()});
 * if (clear && *clear)
 *     ...  // nothing solid between the two points
 * @endcode
 */
class Trace
{
public:
    /** @p bindings must outlive this service; the Runtime declares it above. */
    explicit Trace(const Bindings& bindings) : _bindings(bindings) {}
    Trace(const Trace&) = delete;
    Trace& operator=(const Trace&) = delete;

    /** Unsupported when the Nav_TraceLine slot did not bind. */
    Status Available() const;

    /** Trace a line from @p from to @p to. */
    Result<TraceHit> Line(const Vector& from, const Vector& to, const TraceOptions& options = {}) const;

    /** Sweep the box @p mins..@p maxs, relative to the path, from @p from to @p to. `End` is where
     *  the box's origin stopped. Unsupported when the Nav_TraceShape slot did not bind. */
    Result<TraceHit> Hull(const Vector& from, const Vector& to, const Vector& mins, const Vector& maxs,
                          const TraceOptions& options = {}) const;

    /** True when nothing in the chosen layers lies between the two points. */
    Result<bool> Clear(const Vector& from, const Vector& to, const TraceOptions& options = {}) const;

private:
    const Bindings& _bindings;
};

}  // namespace VoltMod
