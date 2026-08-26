#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/HitGroup.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <cstdint>

namespace VoltMod
{

/** One incoming damage event, as a listener sees it. Observation only - see @ref Damage. */
struct DamageView
{
    int VictimSlot = -1;                 /**< -1 when the victim is not a player pawn */
    int AttackerSlot = -1;               /**< -1 for world damage (fall, fire, the bomb) */
    HitGroup Hitbox = HitGroup::Generic; /**< Invalid when the damage carries no trace */
    uint32_t DamageTypes = 0;            /**< DMG_* bits from the originating info */
    float Damage = 0.0f;                 /**< the incoming amount, before armor and hitgroup scaling */
};

/**
 * @brief Manual vtable hook on CCSPlayerPawn::OnTakeDamage_Alive - every point of damage a living
 * player takes (gamedata offset "OnTakeDamageAlive").
 *
 * **Observation only.** The engine has already turned CTakeDamageInfo into the result it applies
 * by the time listeners run, so nothing a listener does here changes what the victim loses -
 * measured in game, MRES_SUPERCEDE and writes to both structs are ignored, as is writing the
 * victim's health from a listener. The write path was therefore removed rather than left as a
 * trap; a feature that must alter the outcome drives the game's own rules instead, and
 * `mp_damage_headshot_only` and the `mp_damage_scale_*` multipliers do work.
 *
 * Like Movement this binds the class vtable (RTTI on Windows, ELF symbol on Linux), so the first
 * subscription can install it from OnLoad with no player connected, and it then covers every pawn.
 * Dropping the last subscription removes it again.
 *
 * The vtable index and every field offset are gamedata-maintained and drift with CS2 updates. A
 * wrong index calls an unrelated vfunc and crashes, so re-verify after every update; anything that
 * fails to resolve leaves the hook uninstalled and every handler silent rather than guessing.
 */
class Damage
{
public:
    /** Both must outlive this hook; the Runtime declares them above it. */
    Damage(EntitySystem& entities, const Bindings& bindings);
    ~Damage();
    Damage(const Damage&) = delete;
    Damage& operator=(const Damage&) = delete;

    /** One point of damage a living player took. Subscribing installs the hook. */
    Event<const DamageView&> Hit;

private:
    bool Acquire();
    void ReleaseRef();

    bool Hook_OnTakeDamageAlive(void* result);

    /** Walk info -> trace -> hitbox for the struck hitgroup. */
    HitGroup ReadHitGroup(void* info) const;

    EntitySystem& _entities;
    const Bindings& _bindings;
    int _refs = 0;     // live subscriptions on Hit
    VtableHook _hook;  // the pre hook on the pawn class vtable; removed by dropping it
};

}  // namespace VoltMod
