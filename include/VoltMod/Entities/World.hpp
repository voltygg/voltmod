#pragma once

#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Net/NetChannel.hpp>
#include <VoltMod/Engine/Server/Precache.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/Items.hpp>
#include <VoltMod/Entities/Pawns.hpp>
#include <VoltMod/Entities/Rounds.hpp>
#include <VoltMod/Entities/Trace.hpp>

namespace VoltMod
{

/**
 * @brief World-affecting services with no per-tick engine hook of their own: entity IO, weapon
 * give/strip, precaching, pawn manipulation, per-client net-channel reads, line traces, and
 * ending the round.
 *
 * Declared once by Runtime, right after @ref EntitySystem; each member below takes exactly the
 * sibling services it uses, stated once here rather than once per member on Runtime itself.
 */
struct WorldServices
{
    WorldServices(EntitySystem& entities, Bindings& bindings, Scheduler& scheduler, SlotEvents& slots,
                  Interfaces& interfaces)
        : EntityOps(entities, bindings),
          Items(bindings),
          Precache(bindings),
          Pawns(scheduler, slots, entities),
          NetChannels(interfaces),
          Trace(bindings),
          Rounds(entities, bindings)
    {}

    /** Depends on: Entities, Bindings. */
    VoltMod::EntityOps EntityOps;
    /** Weapon give/strip through CCSPlayer_ItemServices. Depends on: Bindings. */
    VoltMod::Items Items;
    /** Depends on: Bindings. */
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
