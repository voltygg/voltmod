#pragma once

namespace CS2Kit
{
class Runtime;
}
namespace CS2Kit::Core
{
struct PluginPolicy;
class SlotEvents;
class Translations;
}  // namespace CS2Kit::Core
namespace CS2Kit::Menu
{
class MenuManager;
}

/**
 * The kit's one ambient pointer to the live @ref CS2Kit::Runtime.
 *
 * Headers below the composition root cannot include <CS2Kit/Runtime.hpp> - it holds every
 * service by value, so pulling it into a Sdk or Menu header would push the whole graph into
 * every translation unit that includes them. They reach the runtime through here instead:
 * one pointer, set on Load and cleared on Unload, rather than a service locator per layer.
 *
 * A .cpp below the root may include <CS2Kit/Runtime.hpp>, and most do - Rt() hands back a
 * Runtime&, which is unusable without the definition. That is a link-time edge, not a header
 * dependency, so it does not propagate. The per-service accessors below exist for the cases
 * that must stay header-only: a template body needs one service, not the whole runtime.
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

// One service at a time, for header-only callers that would otherwise have to include the
// whole composition root. Each aborts on the same condition as Rt().
Core::SlotEvents& Slots();
Core::Translations& Translations();
Core::PluginPolicy& Policy();
Menu::MenuManager& Menus();

}  // namespace CS2Kit::Detail
