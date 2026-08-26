#include "Engine/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Hooks/Damage.hpp>
#include <cstdint>

PLUGIN_GLOBALVARS();

namespace VoltMod::Hooks
{
using namespace VoltMod::Core;

// bool CCSPlayerPawn::OnTakeDamage_Alive(CTakeDamageResult*). void* stands in for the result: the
// SDK declares neither it nor CTakeDamageInfo, so their fields are reached by gamedata offset.
SH_DECL_MANUALHOOK1(VoltMod_OnTakeDamageAlive, 0, 0, 0, bool, void*);

namespace
{

constexpr const char* ServerModule = "server";
constexpr const char* PawnClass = "CCSPlayerPawn";

// Both structs are a few hundred bytes; anything past this is a typo rather than a field.
constexpr int MaxDamageOffset = 512;

}  // namespace

Entities::HitGroup Damage::ReadHitGroup(void* info) const
{
    // The hitgroup is on the hitbox the trace struck, not on CTakeDamageInfo - m_iHitGroupId reads
    // -1 for ordinary bullet damage. Damage with no trace (fire, the bomb, a fall) stays Invalid,
    // which is what keeps the aim rules off world damage.
    auto* trace = Engine::ReadAt<void*>(info, _offsetTrace);
    if (!trace)
        return Entities::HitGroup::Invalid;
    auto* hitbox = Engine::ReadAt<void*>(trace, _offsetTraceHitbox);
    if (!hitbox)
        return Entities::HitGroup::Invalid;
    return static_cast<Entities::HitGroup>(Engine::ReadAt<int32_t>(hitbox, _offsetHitboxGroup));
}

bool Damage::Install()
{
    if (_installed)
        return true;

    int index = _gameData.GetVtableIndex("OnTakeDamageAlive");
    if (index < 0)
        return false;

    if (!ResolveOffsets())
        return false;

    void* vtable = Engine::FindVirtualTable(ServerModule, PawnClass);
    if (!vtable)
        return false;  // Engine::FindVirtualTable already logged which step failed

    SH_MANUALHOOK_RECONFIGURE(VoltMod_OnTakeDamageAlive, index, 0, 0);
    _hookId = SH_ADD_MANUALDVPHOOK(VoltMod_OnTakeDamageAlive, vtable, SH_MEMBER(this, &Damage::Hook_OnTakeDamageAlive),
                                   false);
    if (_hookId == 0)
    {
        Log::Warn("Damage: SourceHook refused the OnTakeDamage_Alive hook; damage listeners disabled.");
        return false;
    }

    _installed = true;
    Log::Info("Damage OnTakeDamage_Alive hook installed on {} vtable (index {}).", PawnClass, index);
    return true;
}

bool Damage::ResolveOffsets()
{
    // A wrong offset silently reads the wrong bytes, so a missing one leaves the hook uninstalled
    // rather than letting listeners act on nonsense.
    struct Entry
    {
        const char* Name;
        int Alignment;
        int* Target;
    };
    const Entry entries[]{
        {"TakeDamageInfoAttacker", alignof(uint32_t), &_offsetAttacker},
        {"TakeDamageInfoDamage", alignof(float), &_offsetDamage},
        {"TakeDamageInfoDamageTypes", alignof(int32_t), &_offsetDamageTypes},
        {"TakeDamageInfoTrace", alignof(void*), &_offsetTrace},
        {"GameTraceHitbox", alignof(void*), &_offsetTraceHitbox},
        {"HitboxGroupId", alignof(int32_t), &_offsetHitboxGroup},
    };

    for (const auto& entry : entries)
    {
        *entry.Target = _gameData.GetByteOffset(entry.Name, MaxDamageOffset, entry.Alignment);
        if (*entry.Target < 0)
        {
            Log::Warn("Damage: no usable '{}' offset; damage listeners disabled.", entry.Name);
            return false;
        }
    }
    return true;
}

void Damage::Remove()
{
    if (!_installed)
        return;

    // Removal by id never dereferences a hooked instance, so this is safe even after a map change
    // has destroyed every pawn.
    SH_REMOVE_HOOK_ID(_hookId);
    _hookId = 0;
    _installed = false;
}

bool Damage::Hook_OnTakeDamageAlive(void* result)
{
    // Fires per point of damage per living player, so an unused hook must cost only this check.
    if (_listeners.Empty())
        RETURN_META_VALUE(MRES_IGNORED, false);

    auto* pawn = META_IFACEPTR(void);
    if (!pawn || !result)
        RETURN_META_VALUE(MRES_IGNORED, false);

    // m_pOriginatingInfo is the result's first member, so it needs no gamedata entry.
    auto* info = Engine::ReadAt<void*>(result, 0);
    if (!info)
        RETURN_META_VALUE(MRES_IGNORED, false);

    const DamageView view{.VictimSlot = _entities.SlotFromPawn(static_cast<CEntityInstance*>(pawn)),
                          .AttackerSlot = _entities.SlotFromPawn(
                              _entities.ResolveEntityHandle(Engine::ReadAt<uint32_t>(info, _offsetAttacker))),
                          .Hitbox = ReadHitGroup(info),
                          .DamageTypes = static_cast<uint32_t>(Engine::ReadAt<int32_t>(info, _offsetDamageTypes)),
                          .Damage = Engine::ReadAt<float>(info, _offsetDamage)};

    // Nothing is written back: the engine ignores every attempt to change the outcome here, so
    // the hook reports and always defers. See the header.
    _listeners.Dispatch([&view](const Callback& callback) { callback(view); });

    RETURN_META_VALUE(MRES_IGNORED, false);
}

}  // namespace VoltMod::Hooks
