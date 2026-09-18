#pragma once

#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Hooks/ClientConVars.hpp>
#include <VoltMod/Hooks/Movement.hpp>
#include <VoltMod/Hooks/Teleport.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Hooks/Vote.hpp>

namespace VoltMod
{

/**
 * @brief Engine hooks grouped by function.
 *
 * Most hooks install on first subscription and remove after the last one. ClientConVars is
 * initialized by Runtime::Start.
 */
struct HookServices
{
    HookServices(EntitySystem& entities, Bindings& bindings, SlotEvents& slots, Scheduler& scheduler,
                 GameEvents& gameEvents, Interfaces& interfaces, EntityOps& entityOps)
        : Movement(entities, bindings),
          Visibility(entities, bindings, slots, entityOps),
          ChatInput(scheduler, slots),
          Teleport(entities, bindings),
          ClientConVars(interfaces, bindings, slots),
          Vote(interfaces, entities, gameEvents, scheduler)
    {}

    VoltMod::Movement Movement;
    VoltMod::Visibility Visibility;
    VoltMod::ChatInput ChatInput;
    VoltMod::Teleport Teleport;
    VoltMod::ClientConVars ClientConVars;
    VoltMod::Vote Vote;
};

}  // namespace VoltMod
