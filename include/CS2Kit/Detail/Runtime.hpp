#pragma once

namespace CS2Kit
{
class Runtime;
}

/**
 * The kit's one ambient pointer to the live @ref CS2Kit::Runtime.
 *
 * Modules below the composition root cannot include <CS2Kit/Runtime.hpp> - it holds every
 * service by value, so including it from Sdk or Menu would invert the layering. They reach
 * the runtime through here instead: one pointer, set on Load and cleared on Unload, rather
 * than a service locator per layer.
 *
 * This is deliberately `Detail`. Plugins get the runtime handed to them by reference and
 * should never call these. The layer check exempts this directory (see modgraph.py).
 */
namespace CS2Kit::Detail
{

/** Point the accessors at @p runtime, or clear them with nullptr. Called by MetamodPlugin. */
void SetRt(Runtime* runtime);

/** The live runtime. Aborts with a log line if called outside a Load/Unload window. */
Runtime& Rt();

/** The live runtime, or nullptr - for teardown paths that may run after the clear. */
Runtime* RtOrNull();

}  // namespace CS2Kit::Detail
