#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Hooks/Damage.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <cstdint>
#include <utility>

namespace VoltMod
{

// bool CCSPlayerPawn::OnTakeDamage_Alive(CTakeDamageResult*). void* stands in for the result: the
// SDK declares neither it nor CTakeDamageInfo, so their fields are reached by gamedata offset.
VOLTMOD_VHOOK1(VoltMod_OnTakeDamageAlive, bool, void*);

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

Damage::~Damage()
{
    // A Subscription that outlives this service would leave the vtable hook live across a
    // meta reload, calling a handler in an unloaded module.
    if (_refs != 0)
        Log::Error("Damage: {} subscription(s) outlived the hook; a damage handler may dangle.", _refs);
}

bool Damage::Acquire()
{
    if (_refs == 0)
    {
        // A wrong offset silently reads the wrong bytes, so a missing one leaves the hook
        // uninstalled rather than letting handlers act on nonsense.
        if (!_bindings.TakeDamage.Attacker || !_bindings.TakeDamage.Damage || !_bindings.TakeDamage.DamageTypes ||
            !_bindings.TakeDamage.Trace || !_bindings.GameTraceHitbox || !_bindings.HitboxGroupId)
        {
            Log::Warn("Damage: gamedata is missing a damage field offset; damage handlers will not fire.");
            return false;
        }

        auto hook = VtableHook::OnVTable<VoltMod_OnTakeDamageAliveHook>(
            "Damage OnTakeDamage_Alive", _bindings.PlayerPawn, _bindings.OnTakeDamageAlive.Index(), this,
            &Damage::Hook_OnTakeDamageAlive, nullptr);
        if (!hook)
        {
            Log::Warn("Damage: {}; damage handlers will not fire.", hook.error().Detail);
            return false;
        }
        _hook = std::move(*hook);
    }
    ++_refs;
    return true;
}

void Damage::ReleaseRef()
{
    if (_refs > 0 && --_refs == 0)
        _hook.Reset();
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

    const DamageView view{.VictimSlot = _entities.SlotOf(Pawn{_entities, static_cast<CEntityInstance*>(pawn)}),
                          .AttackerSlot = _entities.SlotOf(Pawn{
                              _entities, _entities.Resolve(EntityRef{_bindings.TakeDamage.Attacker.Read(info)}).Raw()}),
                          .Hitbox = ReadHitGroup(info),
                          .DamageTypes = static_cast<uint32_t>(_bindings.TakeDamage.DamageTypes.Read(info)),
                          .Damage = _bindings.TakeDamage.Damage.Read(info)};

    // Nothing is written back: the engine ignores every attempt to change the outcome here, so
    // the hook reports and always defers. See the header.
    Hit.Raise(view);

    RETURN_META_VALUE(MRES_IGNORED, false);
}

}  // namespace VoltMod
