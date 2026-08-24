#pragma once

class CGlobalVars;

namespace VoltMod::Sdk
{

/**
 * @brief The engine's simulation clock, read straight from IVEngineServer2::GetServerGlobals().
 *
 * Free functions rather than a service: they hold no state, and are the timestamp source for
 * anything that must line up with the tick the engine is simulating. Both reset when a map starts,
 * so never persist a value across a map change.
 */

/** The engine's CGlobalVars, or nullptr before load / after shutdown. */
CGlobalVars* GetServerGlobals();

/** Current simulation tick (`tickcount`), or 0 when the globals are unavailable. */
int ServerTick();

/** Current simulation time in seconds (`curtime`), or 0 when the globals are unavailable. */
float ServerTime();

}  // namespace VoltMod::Sdk
