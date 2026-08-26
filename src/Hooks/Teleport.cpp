#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Events/EventTypes.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/Teleport.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <mathlib/vector.h>
#include <utility>

namespace VoltMod
{

// CBaseEntity::Teleport(const Vector*, const QAngle*, const Vector*). Bound per pawn
// (Hook_Normal), so the handler runs only for the instance it was added on - one call per
// teleport, no matter how many are bound.
VOLTMOD_VHOOK3_VOID(VoltMod_EntityTeleport, const Vector*, const QAngle*, const Vector*);

Teleport::Teleport(EntitySystem& entities, const Bindings& bindings, GameEvents& events, SlotEvents& slots)
    : Teleported({.OnFirst =
                      [this] {
                          if (!_bindings.Teleport)
                          {
                              Log::Warn(
                                  "Teleport: the Teleport vtable index did not bind; teleports will not be tracked.");
                              return false;
                          }
                          BindAll();
                          return true;
                      },
                  .OnLast = [this] { UnbindAll(); }}),
      _entities(entities),
      _bindings(bindings),
      _events(events),
      _slots(slots)
{}

Teleport::~Teleport()
{
    // A Subscription that outlives this service would leave per-pawn hooks live across a
    // meta reload, calling a handler in an unloaded module.
    if (!Teleported.Empty())
        Log::Error("Teleport: {} subscription(s) outlived the tracker; a handler may dangle.", Teleported.Count());
    UnbindAll();
}

void Teleport::BindAll()
{
    _armed = true;
    for (int slot = 0; slot < MaxPlayers; ++slot)
        Bind(slot);

    // Spawning hands the player a new pawn object, so the old binding is stale - and the spawn
    // placement is itself a teleport worth reporting.
    _spawnListener = _events.On<PlayerSpawn>([this](const PlayerSpawn& e) {
        Bind(e.Slot);
        Teleported.Raise(e.Slot);
    });
    _slotListener = _slots.Changed += [this](int slot) { Unbind(slot); };

    Log::Info("Teleport tracking enabled (vtable index {}).", _bindings.Teleport.Index());
}

void Teleport::UnbindAll()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
        Unbind(slot);

    _spawnListener.Reset();
    _slotListener.Reset();
    _armed = false;
}

void Teleport::OnServerStartup()
{
    // Every pawn from the previous map is gone, so drop the bindings before their addresses are
    // recycled. Removal is by hook id, which SourceHook resolves without touching the freed
    // instance.
    for (int slot = 0; slot < MaxPlayers; ++slot)
        Unbind(slot);
}

void Teleport::Bind(int slot)
{
    if (!_armed || !IsValidSlot(slot))
        return;

    Unbind(slot);

    void* pawn = _entities.PawnOf(slot).Raw();
    if (!pawn)
        return;

    // A freed pawn's address can be handed straight to another player's new one, and nothing tells
    // us the old object died. Drop any slot still claiming this address first: leaving it would make
    // SlotFromPawn resolve every teleport of this pawn to that stale slot, and leave a per-instance
    // hook registered on the address that fires the handler a second time.
    for (int other = 0; other < MaxPlayers; ++other)
        if (other != slot && _pawns[other] == pawn)
            Unbind(other);

    auto hook = VtableHook::OnInstance<VoltMod_EntityTeleportHook>("Teleport", pawn, _bindings.Teleport.Index(), this,
                                                                   &Teleport::Hook_Teleport, true);
    if (!hook)
        return;

    _pawns[slot] = pawn;
    _hooks[slot] = std::move(*hook);
}

void Teleport::Unbind(int slot)
{
    if (!IsValidSlot(slot))
        return;

    _hooks[slot].Reset();
    _pawns[slot] = nullptr;
}

int Teleport::SlotFromPawn(const void* pawn) const
{
    if (!pawn)
        return -1;

    for (int slot = 0; slot < MaxPlayers; ++slot)
        if (_pawns[slot] == pawn)
            return slot;

    return -1;
}

void Teleport::Hook_Teleport(const Vector*, const QAngle*, const Vector*)
{
    Teleported.Raise(SlotFromPawn(META_IFACEPTR(void)));
    RETURN_META(MRES_IGNORED);
}

}  // namespace VoltMod
