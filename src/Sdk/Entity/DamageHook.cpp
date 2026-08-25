#include "Sdk/Internal/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/MetamodGlobals.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Sdk/Engine/GameData.hpp>
#include <VoltMod/Sdk/Engine/MemoryAccess.hpp>
#include <VoltMod/Sdk/Entity/DamageHook.hpp>
#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <VoltMod/Sdk/Entity/PlayerController.hpp>
#include <cstddef>

PLUGIN_GLOBALVARS();

namespace VoltMod::Sdk
{
using namespace VoltMod::Core;

// bool CCSPlayerPawn::OnTakeDamage_Alive(CTakeDamageResult*). void* stands in for the result,
// whose fields are read through the checked layout below rather than a real type. Bound to the
// class vtable (DVP hook), so it fires for every pawn without needing a live instance.
SH_DECL_MANUALHOOK1(VoltMod_OnTakeDamageAlive, 0, 0, 0, bool, void*);

namespace
{

// The server module owning the concrete pawn class every player instantiates.
constexpr const char* ServerModule = "server";
constexpr const char* PawnClass = "CCSPlayerPawn";

/**
 * The two engine structs, transcribed only as far as the fields this hook touches.
 *
 * They are laid out by hand because the SDK does not declare either one. The trailing padding
 * and the static_asserts are the guard: if a CS2 update moves a field, the total size almost
 * certainly changes too and this stops compiling, which is a much better failure than reading
 * the wrong offset at runtime. Sizes are from CS2Fixes' cs2_sdk/entity/ctakedamageinfo.h.
 */
struct TakeDamageInfoLayout
{
    uint8_t _pad0[0x3c];
    uint32_t AttackerHandle;  // 0x3c m_hAttacker
    uint32_t _pad1;           // 0x40 m_hAbility
    float Damage;             // 0x44 m_flDamage
    float _pad2;              // 0x48 m_flTotalledDamage
    int32_t DamageTypes;      // 0x4c m_bitsDamageType
    uint8_t _pad3[0x28];      // 0x50
    int32_t HitGroupId;       // 0x78 m_iHitGroupId
    uint8_t _pad4[0x9c];      // 0x7c
};
static_assert(offsetof(TakeDamageInfoLayout, AttackerHandle) == 0x3c);
static_assert(offsetof(TakeDamageInfoLayout, Damage) == 0x44);
static_assert(offsetof(TakeDamageInfoLayout, DamageTypes) == 0x4c);
static_assert(offsetof(TakeDamageInfoLayout, HitGroupId) == 0x78);
static_assert(sizeof(TakeDamageInfoLayout) == 280);

struct TakeDamageResultLayout
{
    TakeDamageInfoLayout* OriginatingInfo;  // 0x00 m_pOriginatingInfo
    uint8_t _pad0[0x18];                    // 0x08 lean vector + health lost/before
    float DamageDealt;                      // 0x20 m_flDamageDealt
    uint8_t _pad1[0x2c];                    // 0x24
    bool WasDamageSuppressed;               // 0x50 m_bWasDamageSuppressed
    uint8_t _pad2[0xf];                     // 0x51
};
static_assert(offsetof(TakeDamageResultLayout, DamageDealt) == 0x20);
static_assert(offsetof(TakeDamageResultLayout, WasDamageSuppressed) == 0x50);
static_assert(sizeof(TakeDamageResultLayout) == 96);

}  // namespace

bool DamageHook::Install()
{
    if (_installed)
        return true;

    int index = _gameData.GetVtableIndex("OnTakeDamageAlive");
    if (index < 0)
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

int DamageHook::SlotFromPawn(void* pawn) const
{
    if (!pawn)
        return -1;

    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
    {
        PlayerController controller(const_cast<EntitySystem&>(_entities), slot);
        if (controller.IsValid() && controller.GetPawn() == pawn)
            return slot;
    }
    return -1;
}

bool DamageHook::Hook_OnTakeDamageAlive(void* result)
{
    auto* pawn = META_IFACEPTR(void);
    auto* damage = static_cast<TakeDamageResultLayout*>(result);
    if (!pawn || !damage || !damage->OriginatingInfo)
        RETURN_META_VALUE(MRES_IGNORED, false);

    const auto* info = damage->OriginatingInfo;

    DamageView view{.VictimSlot = SlotFromPawn(pawn),
                    .AttackerSlot = SlotFromPawn(_entities.ResolveEntityHandle(info->AttackerHandle)),
                    .Hitbox = static_cast<HitGroup>(info->HitGroupId),
                    .DamageTypes = static_cast<uint32_t>(info->DamageTypes),
                    .Damage = damage->DamageDealt,
                    .Suppress = false};

    const float original = view.Damage;
    _listeners.Dispatch([&view](const Callback& callback) { callback(view); });

    if (view.Suppress)
    {
        damage->DamageDealt = 0.0f;
        damage->WasDamageSuppressed = true;
        RETURN_META_VALUE(MRES_SUPERCEDE, false);
    }

    // Only write when a listener actually changed it, so the untouched path leaves the engine's
    // own value bit-for-bit alone.
    if (view.Damage != original)
        damage->DamageDealt = view.Damage;

    RETURN_META_VALUE(MRES_IGNORED, false);
}

}  // namespace VoltMod::Sdk
