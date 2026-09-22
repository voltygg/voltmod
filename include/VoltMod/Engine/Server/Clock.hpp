#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>

namespace VoltMod
{

/**
 * @brief The engine's simulation clock, read straight from IVEngineServer2::GetServerGlobals().
 *
 * Holds no state of its own - every call reads the engine live. It is the timestamp source for
 * anything that must line up with the tick the engine is simulating. Both readings reset when a
 * map starts, so never persist a value across a map change.
 *
 * Exposed as `runtime.Clock`.
 */
class Clock
{
public:
    /** @p interfaces supplies IVEngineServer2; it must outlive this service. */
    explicit Clock(Interfaces& interfaces);
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;

    /** The engine's CGlobalVars, or nullptr before load / after shutdown. */
    CGlobalVars* Globals() const;

    /** Current simulation tick (`tickcount`), or 0 when the globals are unavailable. */
    int Tick() const;

    /** Current simulation time in seconds (`curtime`), or 0 when the globals are unavailable. */
    float Time() const;

private:
    Interfaces& _interfaces;
};

}  // namespace VoltMod
