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
#include <VoltMod/Hooks/Damage.hpp>
#include <VoltMod/Hooks/InputHistory.hpp>
#include <VoltMod/Hooks/Movement.hpp>
#include <VoltMod/Hooks/Teleport.hpp>
#include <VoltMod/Hooks/Transmit.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Messaging/Vote.hpp>

namespace VoltMod
{

/**
 * @brief The per-tick and per-event engine hooks, grouped because every one of them is dormant
 * until a plugin subscribes - or, for ClientCvars, until Enable() is called.
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
          Damage(entities, bindings),
          Transmit(entities, bindings, slots),
          Visibility(entities, entityOps, Transmit),
          ChatInput(scheduler, slots),
          InputHistory(Movement, slots),
          Teleport(entities, bindings, gameEvents, slots),
          ClientCvars(interfaces, bindings, slots),
          Vote(interfaces, entities, gameEvents, scheduler)
    {}

    /** Dormant until something subscribes; the last subscription dropped removes the vtable
     *  hook. Depends on: Entities, Bindings. */
    VoltMod::Movement Movement;
    /** Dormant until something subscribes to Hit; observation only - listeners see each hit but
     *  cannot change it. Depends on: Entities, Bindings. */
    VoltMod::Damage Damage;
    /** Depends on: Entities, Bindings, Slots. */
    VoltMod::Transmit Transmit;
    /** Builds per-viewer visibility effects (GlowVision). Depends on: Entities, EntityOps,
     *  Transmit. */
    VoltMod::Visibility Visibility;
    /** Depends on: Scheduler, Slots. */
    VoltMod::ChatInput ChatInput;
    /** Dormant until Enable(depth); listens on the Movement cmd feed + slot changes.
     *  Depends on: Movement, Slots. */
    VoltMod::InputHistory InputHistory;
    /** Dormant until something subscribes to Teleported; per-pawn Teleport hook re-bound on
     *  PlayerSpawn. Depends on: Entities, Bindings, GameEvents, Slots. */
    VoltMod::Teleport Teleport;
    /** Async client-side convar reads. Inert when Capability::ClientCvars is off.
     *  Depends on: Interfaces, Bindings, Slots. */
    VoltMod::ClientCvars ClientCvars;
    /** The game's own yes/no vote panel. Subscribes on the first StartVote().
     *  Depends on: Interfaces, Entities, GameEvents, Scheduler. */
    VoltMod::Vote Vote;
};

}  // namespace VoltMod
