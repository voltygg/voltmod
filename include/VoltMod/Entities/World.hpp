#pragma once

#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Net/NetChannel.hpp>
#include <VoltMod/Engine/Server/Precache.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/Pawns.hpp>
#include <VoltMod/Entities/Rounds.hpp>
#include <VoltMod/Entities/Trace.hpp>

namespace VoltMod
{

/** Services that act on the world without an engine hook of their own; each member lists the
 *  services it depends on. */
struct WorldServices
{
    WorldServices(EntitySystem& entities, Bindings& bindings, Scheduler& scheduler, SlotEvents& slots,
                  Interfaces& interfaces)
        : Pawns(scheduler, slots, entities), NetChannels(interfaces), Trace(bindings), Rounds(entities, bindings)
    {}

    /** Resources for the next map's session manifest. */
    VoltMod::Precache Precache;
    /** Pawn manipulations that need framework services, such as slap and its fall protection.
     *  Depends on: Scheduler, Slots, Entities. */
    VoltMod::Pawns Pawns;
    /** Stateless per-client net-channel reads (latency, replicated userinfo cvars).
     *  Depends on: Interfaces. */
    VoltMod::NetChannels NetChannels;
    /** Line traces for sight and reachability questions. Depends on: Bindings. */
    VoltMod::Trace Trace;
    /** Ending the current round with a winner. Depends on: Entities, Bindings. */
    VoltMod::Rounds Rounds;
};

}  // namespace VoltMod
