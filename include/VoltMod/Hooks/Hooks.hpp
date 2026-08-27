#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Hooks/ClientCvars.hpp>
#include <VoltMod/Hooks/HudClicks.hpp>
#include <VoltMod/Hooks/Movement.hpp>
#include <VoltMod/Hooks/Teleport.hpp>
#include <VoltMod/Hooks/Transmit.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Hooks/Vote.hpp>

namespace VoltMod
{

/**
 * @brief The per-tick and per-event engine hooks, grouped because every one of them is dormant
 * until a plugin subscribes - or, for ClientCvars, until Runtime::Start calls its Initialize().
 *
 * Declared once by Runtime, after @ref WorldServices (Visibility needs its EntityOps) and
 * @ref GameEvents (Teleport and Vote need it); each member below takes exactly the sibling
 * services it uses, stated once here rather than once per member on Runtime itself.
 */
struct HookServices
{
    HookServices(EntitySystem& entities, Bindings& bindings, SlotEvents& slots, Scheduler& scheduler,
                 GameEvents& gameEvents, Interfaces& interfaces, EntityOps& entityOps)
        : Movement(entities, bindings),
          Transmit(entities, bindings, slots),
          Visibility(entities, entityOps, Transmit),
          ChatInput(scheduler, slots),
          Teleport(entities, bindings, gameEvents, slots),
          ClientCvars(interfaces, bindings, slots),
          Vote(interfaces, entities, gameEvents, scheduler),
          HudClicks(interfaces, bindings, slots)
    {}

    /** Dormant until something subscribes; the last subscription dropped removes the vtable
     *  hook. Depends on: Entities, Bindings. */
    VoltMod::Movement Movement;
    /** Depends on: Entities, Bindings, Slots. */
    VoltMod::Transmit Transmit;
    /** Builds per-viewer visibility effects (GlowVision). Depends on: Entities, EntityOps,
     *  Transmit. */
    VoltMod::Visibility Visibility;
    /** Depends on: Scheduler, Slots. */
    VoltMod::ChatInput ChatInput;
    /** Dormant until something subscribes to Teleported; per-pawn Teleport hook re-bound on
     *  PlayerSpawn. Depends on: Entities, Bindings, GameEvents, Slots. */
    VoltMod::Teleport Teleport;
    /** Async client-side convar reads. Inert when Capability::ClientCvars is off.
     *  Depends on: Interfaces, Bindings, Slots. */
    VoltMod::ClientCvars ClientCvars;
    /** The game's own yes/no vote panel. Subscribes on the first StartVote().
     *  Depends on: Interfaces, Entities, GameEvents, Scheduler. */
    VoltMod::Vote Vote;
    /** Button presses coming back from a custom HUD layout. Dormant until something subscribes,
     *  and it binds from a live client, so an empty server arms on the first connect.
     *  Depends on: Interfaces, Bindings, Slots. */
    VoltMod::HudClicks HudClicks;
};

}  // namespace VoltMod
