#include "Sdk/Internal/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/MetamodGlobals.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Sdk/Engine/GameData.hpp>
#include <VoltMod/Sdk/Engine/MemoryAccess.hpp>
#include <VoltMod/Sdk/Entity/DamageHook.hpp>
#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <cstdint>

PLUGIN_GLOBALVARS();

namespace VoltMod::Sdk
{
using namespace VoltMod::Core;

// bool CCSPlayerPawn::OnTakeDamage_Alive(CTakeDamageResult*). void* stands in for the result:
// the SDK declares neither it nor CTakeDamageInfo, so their fields are reached by gamedata
// offset. Bound to the class vtable (DVP hook), so it fires for every pawn without a live one.
SH_DECL_MANUALHOOK1(VoltMod_OnTakeDamageAlive, 0, 0, 0, bool, void*);

namespace
{

// The server module owning the concrete pawn class every player instantiates.
constexpr const char* ServerModule = "server";
constexpr const char* PawnClass = "CCSPlayerPawn";

// Bounds the gamedata reads. Both structs are a few hundred bytes; anything past this is a
// typo rather than a field.
constexpr int MaxDamageOffset = 512;

}  // namespace

bool DamageHook::Install()
{
    if (_installed)
        return true;

    int index = _gameData.GetVtableIndex("OnTakeDamageAlive");
    if (index < 0)
        return false;

    if (!ResolveOffsets())
        return false;

    void* vtable = FindVirtualTable(ServerModule, PawnClass);
    if (!vtable)
        return false;  // FindVirtualTable already logged which step failed

    SH_MANUALHOOK_RECONFIGURE(VoltMod_OnTakeDamageAlive, index, 0, 0);
    _hookId = SH_ADD_MANUALDVPHOOK(VoltMod_OnTakeDamageAlive, vtable,
                                   SH_MEMBER(this, &DamageHook::Hook_OnTakeDamageAlive), false);
    if (_hookId == 0)
    {
        Log::Warn("DamageHook: SourceHook refused the OnTakeDamage_Alive hook; damage listeners disabled.");
        return false;
    }

    _installed = true;
    Log::Info("Damage OnTakeDamage_Alive hook installed on {} vtable (index {}).", PawnClass, index);
    return true;
}

bool DamageHook::ResolveOffsets()
{
    // A wrong field offset silently reads or writes the wrong bytes, so a missing one leaves the
    // hook uninstalled rather than letting listeners act on nonsense.
    struct Entry
    {
        const char* Name;
        int Alignment;
        int* Target;
    };
    const Entry entries[]{
        {"TakeDamageInfoAttacker", alignof(uint32_t), &_offsetAttacker},
        {"TakeDamageInfoDamageTypes", alignof(int32_t), &_offsetDamageTypes},
        {"TakeDamageInfoHitGroup", alignof(int32_t), &_offsetHitGroup},
        {"TakeDamageResultDealt", alignof(float), &_offsetDealt},
        {"TakeDamageResultSuppressed", alignof(bool), &_offsetSuppressed},
    };

    for (const auto& entry : entries)
    {
        *entry.Target = _gameData.GetByteOffset(entry.Name, MaxDamageOffset, entry.Alignment);
        if (*entry.Target < 0)
        {
            Log::Warn("DamageHook: no usable '{}' offset; damage listeners disabled.", entry.Name);
            return false;
        }
    }
    return true;
}

void DamageHook::Remove()
{
    if (!_installed)
        return;

    // Removal by id never dereferences a hooked instance, so this is safe even after a map
    // change has destroyed every pawn.
    SH_REMOVE_HOOK_ID(_hookId);
    _hookId = 0;
    _installed = false;
}

bool DamageHook::Hook_OnTakeDamageAlive(void* result)
{
    // Fires for every point of damage every living player takes, so an installed-but-unused
    // hook must cost nothing beyond this check.
    if (_listeners.Empty())
        RETURN_META_VALUE(MRES_IGNORED, false);

    auto* pawn = META_IFACEPTR(void);
    if (!pawn || !result)
        RETURN_META_VALUE(MRES_IGNORED, false);

    // CTakeDamageResult::m_pOriginatingInfo is the first member, so it needs no gamedata entry.
    auto* info = ReadAt<void*>(result, 0);
    if (!info)
        RETURN_META_VALUE(MRES_IGNORED, false);

    DamageView view{
        .VictimSlot = _entities.SlotFromPawn(static_cast<CEntityInstance*>(pawn)),
        .AttackerSlot = _entities.SlotFromPawn(_entities.ResolveEntityHandle(ReadAt<uint32_t>(info, _offsetAttacker))),
        .Hitbox = static_cast<HitGroup>(ReadAt<int32_t>(info, _offsetHitGroup)),
        .DamageTypes = static_cast<uint32_t>(ReadAt<int32_t>(info, _offsetDamageTypes)),
        .Damage = ReadAt<float>(result, _offsetDealt),
        .Suppress = false};

    const float original = view.Damage;
    _listeners.Dispatch([&view](const Callback& callback) { callback(view); });

    if (view.Suppress)
    {
        WriteAt<float>(result, _offsetDealt, 0.0f);
        WriteAt<bool>(result, _offsetSuppressed, true);
        RETURN_META_VALUE(MRES_SUPERCEDE, false);
    }

    // Only write when a listener actually changed it, so the untouched path leaves the engine's
    // own value bit-for-bit alone.
    if (view.Damage != original)
        WriteAt<float>(result, _offsetDealt, view.Damage);

    RETURN_META_VALUE(MRES_IGNORED, false);
}

}  // namespace VoltMod::Sdk
