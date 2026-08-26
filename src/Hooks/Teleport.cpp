#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Clock.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/PlayerController.hpp>
#include <VoltMod/Events/EventTypes.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/Teleport.hpp>
#include <mathlib/vector.h>

namespace VoltMod
{

// CBaseEntity::Teleport(const Vector*, const QAngle*, const Vector*); the vtable index comes from
// gamedata at Enable time. Bound per pawn (Hook_Normal), so the handler runs only for the instance
// it was added on - one call per teleport, no matter how many are bound.
SH_DECL_MANUALHOOK3_void(VoltMod_EntityTeleport, 0, 0, 0, const Vector*, const QAngle*, const Vector*);

Teleport::~Teleport()
{
    Disable();
}

bool Teleport::Enable()
{
    if (_enabled)
        return true;

    int index = _gameData.GetVtableIndex("Teleport");
    if (index < 0)
        return false;

    SH_MANUALHOOK_RECONFIGURE(VoltMod_EntityTeleport, index, 0, 0);
    _enabled = true;

    for (int slot = 0; slot < MaxPlayers; ++slot)
        Bind(slot);

    // Spawning hands the player a new pawn object, so the old binding is stale - and the spawn
    // placement is itself a teleport worth stamping.
    _spawnListener = _events.Listen<PlayerSpawn>([this](const PlayerSpawn& e) {
        Bind(e.Slot);
        Stamp(e.Slot);
    });
    _slotListener = _slots.Listen([this](int slot) {
        Unbind(slot);
        if (IsValidSlot(slot))
            _lastTeleport[slot] = 0.0f;
    });

    Log::Info("Teleport tracking enabled (vtable index {}).", index);
    return true;
}

void Teleport::Disable()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
        Unbind(slot);
    _lastTeleport.fill(0.0f);

    _spawnListener.Reset();
    _slotListener.Reset();
    _enabled = false;
}

bool Teleport::JustTeleported(int slot, float seconds) const
{
    if (!IsValidSlot(slot) || _lastTeleport[slot] == 0.0f)
        return false;

    return _clock.Time() - _lastTeleport[slot] <= seconds;
}

void Teleport::OnServerStartup()
{
    // Every pawn from the previous map is gone, so drop the bindings before their addresses are
    // recycled. Removal is by hook id, which SourceHook resolves without touching the freed
    // instance.
    for (int slot = 0; slot < MaxPlayers; ++slot)
        Unbind(slot);
    _lastTeleport.fill(0.0f);
}

void Teleport::Bind(int slot)
{
    if (!_enabled || !IsValidSlot(slot))
        return;

    Unbind(slot);

    void* pawn = _entities.Controller(slot).GetPawn();
    if (!pawn)
        return;

    // A freed pawn's address can be handed straight to another player's new one, and nothing tells
    // us the old object died. Drop any slot still claiming this address first: leaving it would make
    // SlotFromPawn resolve every teleport of this pawn to that stale slot, and leave a per-instance
    // hook registered on the address that fires the handler a second time.
    for (int other = 0; other < MaxPlayers; ++other)
        if (other != slot && _pawns[other] == pawn)
            Unbind(other);

    _pawns[slot] = pawn;
    _hookIds[slot] = SH_ADD_MANUALHOOK(VoltMod_EntityTeleport, pawn, SH_MEMBER(this, &Teleport::Hook_Teleport), true);
}

void Teleport::Unbind(int slot)
{
    if (!IsValidSlot(slot))
        return;

    if (_hookIds[slot] != 0)
        SH_REMOVE_HOOK_ID(_hookIds[slot]);
    _hookIds[slot] = 0;
    _pawns[slot] = nullptr;
}

void Teleport::Stamp(int slot)
{
    if (IsValidSlot(slot))
        _lastTeleport[slot] = _clock.Time();
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
    Stamp(SlotFromPawn(META_IFACEPTR(void)));
    RETURN_META(MRES_IGNORED);
}

}  // namespace VoltMod
