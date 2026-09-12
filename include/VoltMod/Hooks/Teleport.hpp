#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Core/Subscription.hpp>

namespace VoltMod
{

/**
 * @brief Raises @ref Teleported whenever a player's pawn is moved by CBaseEntity::Teleport.
 *
 * Dormant until something subscribes: it then hooks the "Teleport" slot on the CCSPlayerPawn class
 * vtable, so every pawn sharing it is covered, respawns included. A spawn also moves the player, so
 * **a spawn raises the event too**. Filter spawns yourself if you only care about mid-life
 * teleports.
 *
 * The hook is all this owns. It keeps no history: how long the discontinuity after a teleport
 * matters, and in what clock, is the consumer's question.
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

    /** A pawn was teleported; the argument is its slot (-1 when it belongs to no player).
     *  Subscribing installs the tracker. */
    Event<int> Teleported;

private:
    /** Install the class hook, or refuse the subscription after saying why. */
    bool Install();

    EntitySystem& _entities;
    const Bindings& _bindings;
    Subscription _hook;
};

}  // namespace VoltMod
