#include <CS2Kit/Core/MetamodGlobals.hpp>
#include <CS2Kit/Core/Slot.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/GameData.hpp>
#include <CS2Kit/Sdk/GameEventService.hpp>
#include <CS2Kit/Sdk/GameEvents.hpp>
#include <CS2Kit/Sdk/PlayerController.hpp>
#include <CS2Kit/Sdk/ServerClock.hpp>
#include <CS2Kit/Sdk/TeleportTracker.hpp>
#include <CS2Kit/Utils/Log.hpp>
#include <mathlib/vector.h>

PLUGIN_GLOBALVARS();

namespace CS2Kit::Sdk
{
using namespace CS2Kit::Utils;

// CBaseEntity::Teleport(const Vector*, const QAngle*, const Vector*); the vtable index comes from
// gamedata at Enable time. Bound per pawn (Hook_Normal), so the handler runs only for the instance
// it was added on - one call per teleport, no matter how many are bound.
SH_DECL_MANUALHOOK3_void(CS2Kit_EntityTeleport, 0, 0, 0, const Vector*, const QAngle*, const Vector*);

TeleportTracker::~TeleportTracker()
{
    Disable();
}

bool TeleportTracker::Enable()
{
    if (_enabled)
        return true;

    int index = CS2Kit::Detail::Rt().GameData.GetOffset("Teleport");
    if (index < 0)
    {
        Log::Warn("TeleportTracker: gamedata offset 'Teleport' missing; tracking disabled.");
        return false;
    }

    SH_MANUALHOOK_RECONFIGURE(CS2Kit_EntityTeleport, index, 0, 0);
    _enabled = true;

    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
        Bind(slot);

    // Spawning hands the player a new pawn object, so the old binding is stale - and the spawn
    // placement is itself a teleport worth stamping.
    _spawnListener = CS2Kit::Detail::Rt().Events.Listen<Events::PlayerSpawn>([this](const Events::PlayerSpawn& e) {
        Bind(e.Slot);
        Stamp(e.Slot);
    });
    _slotListener = _slots.Listen([this](int slot) {
        Unbind(slot);
        if (Core::IsValidSlot(slot))
            _lastTeleport[slot] = 0.0f;
    });

    Log::Info("Teleport tracking enabled (vtable index {}).", index);
    return true;
}

void TeleportTracker::Disable()
{
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
        Unbind(slot);
    _lastTeleport.fill(0.0f);

    // _slots is declared ahead of this tracker in Services, so it outlives us either way.
    if (_slotListener != 0)
        _slots.Remove(_slotListener);
    // Runs from the destructor too, where the hub may already be gone.
    if (auto* services = CS2Kit::Detail::RtOrNull(); services && _spawnListener != 0)
        services->Events.RemoveListener(_spawnListener);
    _spawnListener = 0;
    _slotListener = 0;
    _enabled = false;
}

bool TeleportTracker::JustTeleported(int slot, float seconds) const
{
    if (!Core::IsValidSlot(slot) || _lastTeleport[slot] == 0.0f)
        return false;

    return ServerTime() - _lastTeleport[slot] <= seconds;
}

void TeleportTracker::OnServerStartup()
{
    // Every pawn from the previous map is gone, so drop the bindings before their addresses are
    // recycled. Removal is by hook id, which SourceHook resolves without touching the freed
    // instance.
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
        Unbind(slot);
    _lastTeleport.fill(0.0f);
}

void TeleportTracker::Bind(int slot)
{
    if (!_enabled || !Core::IsValidSlot(slot))
        return;

    Unbind(slot);

    void* pawn = PlayerController(slot).GetPawn();
    if (!pawn)
        return;

    // A freed pawn's address can be handed straight to another player's new one, and nothing tells
    // us the old object died. Drop any slot still claiming this address first: leaving it would make
    // SlotFromPawn resolve every teleport of this pawn to that stale slot, and leave a per-instance
    // hook registered on the address that fires the handler a second time.
    for (int other = 0; other < Core::MaxPlayers; ++other)
        if (other != slot && _pawns[other] == pawn)
            Unbind(other);

    _pawns[slot] = pawn;
    _hookIds[slot] =
        SH_ADD_MANUALHOOK(CS2Kit_EntityTeleport, pawn, SH_MEMBER(this, &TeleportTracker::Hook_Teleport), true);
}

void TeleportTracker::Unbind(int slot)
{
    if (!Core::IsValidSlot(slot))
        return;

    if (_hookIds[slot] != 0)
        SH_REMOVE_HOOK_ID(_hookIds[slot]);
    _hookIds[slot] = 0;
    _pawns[slot] = nullptr;
}

void TeleportTracker::Stamp(int slot)
{
    if (Core::IsValidSlot(slot))
        _lastTeleport[slot] = ServerTime();
}

int TeleportTracker::SlotFromPawn(const void* pawn) const
{
    if (!pawn)
        return -1;

    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
        if (_pawns[slot] == pawn)
            return slot;

    return -1;
}

void TeleportTracker::Hook_Teleport(const Vector*, const QAngle*, const Vector*)
{
    Stamp(SlotFromPawn(META_IFACEPTR(void)));
    RETURN_META(MRES_IGNORED);
}

}  // namespace CS2Kit::Sdk
