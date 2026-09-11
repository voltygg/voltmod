#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <array>

namespace VoltMod
{

/**
 * @brief Raises @ref Teleported whenever a player's pawn is moved by CBaseEntity::Teleport.
 *
 * Dormant until something subscribes: it then hooks the "Teleport" vtable index (from gamedata) on
 * every live pawn, and dropping the last subscription unbinds them all again. A pawn is a fresh
 * object after every respawn, so the hook is re-bound on PlayerSpawn - and since a spawn also
 * moves the player, **a spawn raises the event too**. Filter spawns yourself if you only care
 * about mid-life teleports.
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
    /** @p entities resolves each slot's pawn, @p bindings supplies the Teleport vtable index,
     *  @p events the PlayerSpawn re-bind, and @p slots says when a slot changes hands. All four
     *  must outlive it; the Runtime declares them above. */
    Teleport(EntitySystem& entities, const Bindings& bindings, GameEvents& events, SlotEvents& slots);
    ~Teleport();
    Teleport(const Teleport&) = delete;
    Teleport& operator=(const Teleport&) = delete;

    /** A pawn was teleported; the argument is its slot (-1 when it belongs to no player).
     *  Subscribing installs the tracker. */
    Event<int> Teleported;

    /** Drop every binding for the new map. Called by the framework's StartupServer hook. */
    void OnServerStartup();

private:
    /** Bind every live pawn and listen for the spawns and slot changes that invalidate a binding;
     *  the reverse drops all of it. Driven only by Teleported's lifecycle. */
    void BindAll();
    void UnbindAll();

    KHook::Return<void> Hook_Teleport(VtableObject* pawn, const Vector* origin, const QAngle* angles,
                                      const Vector* velocity);

    /** Rebind @p slot to its current pawn (no-op without one), replacing any previous binding. */
    void Bind(int slot);
    void Unbind(int slot);
    int SlotOf(const void* pawn) const;

    EntitySystem& _entities;
    const Bindings& _bindings;
    GameEvents& _events;
    SlotEvents& _slots;
    std::array<void*, MaxPlayers> _pawns{};     // the instance each slot's hook is bound to
    std::array<Subscription, MaxPlayers> _hooks;  // one per bound pawn; empty when unbound
    Subscription _spawnListener;
    Subscription _slotListener;
};

}  // namespace VoltMod
