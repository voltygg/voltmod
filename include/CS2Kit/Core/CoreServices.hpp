#pragma once

#include <CS2Kit/Core/LoadReport.hpp>
#include <CS2Kit/Core/PluginPolicy.hpp>
#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Core/SlotEvents.hpp>
#include <string>

namespace CS2Kit::Core
{

/**
 * @brief The services every other layer is allowed to reach for.
 *
 * Core is the bottom of the stack, so this owns its members outright and depends on
 * nothing. Anything above it - Sdk, Players, Commands, Menu, Database - gets here
 * through @ref Ctx() instead of reaching up to the composition root, which is what
 * keeps the module graph acyclic.
 */
class CoreServices
{
public:
    CoreServices() = default;
    CoreServices(const CoreServices&) = delete;
    CoreServices& operator=(const CoreServices&) = delete;

    /** Plugin-supplied policy (permissions, targeting, replies). Set once in OnLoad. */
    PluginPolicy Policy;
    /** Named/timed load stages recorded by Initialize and the plugin's OnLoad. */
    Core::LoadReport LoadReport;
    /** Frame pump, timers and delayed work. */
    Core::Scheduler Scheduler;
    /** "This slot changed hands", raised by Players and consumed by services beneath it. */
    Core::SlotEvents Slots;
    /** Map the server is running, captured from the StartupServer hook. Empty after a late
     *  (mid-map) load until the next map change, since the hook has already fired by then. */
    std::string CurrentMap;
};

/** Set/clear the active CoreServices. Called by the composition root on Load/Unload. */
void SetActiveCoreServices(CoreServices* services);

/** The active CoreServices. Asserts if called outside a Load/Unload window. */
CoreServices& Ctx();

/** The active CoreServices, or nullptr - for teardown paths that may run after Shutdown. */
CoreServices* CtxOrNull();

}  // namespace CS2Kit::Core
