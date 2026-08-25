#pragma once

namespace VoltMod
{
class Runtime;
}

/**
 * The framework's one ambient pointer to the live @ref VoltMod::Runtime.
 *
 * Services take the runtime (or the narrower service they need) through their constructors.
 * What remains here is the composition root's own bootstrap problem: MetamodPlugin publishes
 * the runtime on Load and clears it on Unload, and the few call sites that the engine or a
 * worker thread enters without a reference of their own read it back.
 *
 * A .cpp may include <VoltMod/Runtime.hpp> to use the result - Rt() hands back a Runtime&,
 * which is unusable without the definition. That is a link-time edge, not a header dependency,
 * so it does not propagate.
 *
 * This is deliberately `Detail`. Plugins get the runtime handed to them by reference and
 * should never call these. The layer check exempts this directory (see modgraph.py).
 */
namespace VoltMod::Detail
{

/** Point the accessors at @p runtime, or clear them with nullptr. Called by MetamodPlugin. */
void SetRt(Runtime* runtime);

/** The live runtime. Aborts with a log line if called outside a Load/Unload window. */
Runtime& Rt();

/** The live runtime, or nullptr - for teardown paths that may run after the clear. */
Runtime* RtOrNull();

}  // namespace VoltMod::Detail
