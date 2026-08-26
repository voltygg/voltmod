#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Hooks/Damage.hpp>
#include <cstdint>
#include <string_view>

namespace VoltMod
{

// bool CCSPlayerPawn::OnTakeDamage_Alive(CTakeDamageResult*). void* stands in for the result: the
// SDK declares neither it nor CTakeDamageInfo, so their fields are reached by gamedata offset.
SH_DECL_MANUALHOOK1(VoltMod_OnTakeDamageAlive, 0, 0, 0, bool, void*);

HitGroup Damage::ReadHitGroup(void* info) const
{
    // The hitgroup is on the hitbox the trace struck, not on CTakeDamageInfo - m_iHitGroupId reads
    // -1 for ordinary bullet damage. Damage with no trace (fire, the bomb, a fall) stays Invalid,
    // which is what keeps the aim rules off world damage.
    void* trace = _bindings.TakeDamage.Trace.Read(info);
    if (!trace)
        return HitGroup::Invalid;
    void* hitbox = _bindings.GameTraceHitbox.Read(trace);
    if (!hitbox)
        return HitGroup::Invalid;
    return static_cast<HitGroup>(_bindings.HitboxGroupId.Read(hitbox));
}

Damage::Damage(EntitySystem& entities, const Bindings& bindings)
    : Hit({.OnFirst = [this] { return Acquire(); }, .OnLast = [this] { ReleaseRef(); }}),
      _entities(entities),
      _bindings(bindings)
{}

bool Damage::Acquire()
{
    if (_refs == 0)
    {
        if (Status installed = Install(); !installed)
        {
            Log::Warn("Damage: {}; damage handlers will not fire.", installed.error().Detail);
            return false;
        }
    }
    ++_refs;
    return true;
}

void Damage::ReleaseRef()
{
    if (_refs > 0 && --_refs == 0)
        Remove();
}

Status Damage::Install()
{
    if (_installed)
        return {};

    if (!_bindings.OnTakeDamageAlive)
        return std::unexpected(Error::Unsupported("gamedata has no 'OnTakeDamageAlive' vtable index"));

    // A wrong offset silently reads the wrong bytes, so a missing one leaves the hook uninstalled
    // rather than letting handlers act on nonsense.
    if (!_bindings.TakeDamage.Attacker || !_bindings.TakeDamage.Damage || !_bindings.TakeDamage.DamageTypes ||
        !_bindings.TakeDamage.Trace || !_bindings.GameTraceHitbox || !_bindings.HitboxGroupId)
        return std::unexpected(Error::Unsupported("gamedata is missing a damage field offset"));

    if (!_bindings.PlayerPawn)
        return std::unexpected(Error::Engine("the player pawn vtable did not bind"));

    const int index = _bindings.OnTakeDamageAlive.Index();
    SH_MANUALHOOK_RECONFIGURE(VoltMod_OnTakeDamageAlive, index, 0, 0);
    _hookId = SH_ADD_MANUALDVPHOOK(VoltMod_OnTakeDamageAlive, _bindings.PlayerPawn.Table(),
                                   SH_MEMBER(this, &Damage::Hook_OnTakeDamageAlive), false);
    if (_hookId == 0)
        return std::unexpected(Error::Engine("SourceHook refused the OnTakeDamage_Alive hook"));

    _installed = true;
    Log::Info("Damage OnTakeDamage_Alive hook installed on {} vtable (index {}).", _bindings.PlayerPawn.Class(), index);
    return {};
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
    if (Hit.Empty())
        RETURN_META_VALUE(MRES_IGNORED, false);

    auto* pawn = META_IFACEPTR(void);
    if (!pawn || !result)
        RETURN_META_VALUE(MRES_IGNORED, false);

    // m_pOriginatingInfo is the result's first member, so it needs no gamedata entry.
    auto* info = ReadAt<void*>(result, 0);
    if (!info)
        RETURN_META_VALUE(MRES_IGNORED, false);

    const DamageView view{
        .VictimSlot = _entities.SlotFromPawn(static_cast<CEntityInstance*>(pawn)),
        .AttackerSlot = _entities.SlotFromPawn(_entities.ResolveEntityHandle(_bindings.TakeDamage.Attacker.Read(info))),
        .Hitbox = ReadHitGroup(info),
        .DamageTypes = static_cast<uint32_t>(_bindings.TakeDamage.DamageTypes.Read(info)),
        .Damage = _bindings.TakeDamage.Damage.Read(info)};

    // Nothing is written back: the engine ignores every attempt to change the outcome here, so
    // the hook reports and always defers. See the header.
    Hit.Raise(view);

    RETURN_META_VALUE(MRES_IGNORED, false);
}

}  // namespace VoltMod
