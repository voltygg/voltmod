#pragma once

#include <VoltMod/Core/Signals/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>

namespace VoltMod
{

/**
 * @brief Report player pawn moves through CBaseEntity::Teleport.
 *
 * The hook installs on the first subscription and covers every pawn sharing the CCSPlayerPawn
 * vtable, including respawns. Spawning raises the event as well, so consumers interested only in
 * mid-life teleports must filter spawn events.
 *
 * The service keeps no teleport history. Consumers decide how long a teleport remains relevant and
 * which clock to use.
 *
 * @code
 * _teleports = runtime.Hooks.Teleport.Teleported += [this](int slot) { _lastTeleport[slot] = _clock.Time(); };
 * @endcode
 */
class Teleport
{
public:
    /** @p entities resolves the teleported pawn's slot and @p bindings supplies the Teleport slot
     *  and class vtable. Both must outlive it; the Runtime declares them above. */
    Teleport(EntitySystem& entities, const Bindings& bindings);
    ~Teleport();
    Teleport(const Teleport&) = delete;
    Teleport& operator=(const Teleport&) = delete;

    /** A pawn was teleported. The slot is -1 when it belongs to no player. */
    Event<int> Teleported;

    /** Why teleports cannot be tracked: the CBaseEntity::Teleport slot did not bind. */
    Status Available() const;

private:
    bool Install();

    EntitySystem& _entities;
    const Bindings& _bindings;
    Subscription _hook;
};

}  // namespace VoltMod
