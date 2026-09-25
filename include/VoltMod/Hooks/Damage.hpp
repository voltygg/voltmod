#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/Event.hpp>
#include <VoltMod/Core/Signals/HookResult.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Engine/EntityRef.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <cstdint>

namespace VoltMod
{

/** @defgroup DamageTypes Damage type bits (DamageTypes_t); combine with `|`. */
/** @{ */
constexpr uint32_t DamageGeneric = 0;
constexpr uint32_t DamageCrush = 1u << 0;
constexpr uint32_t DamageBullet = 1u << 1;
constexpr uint32_t DamageSlash = 1u << 2;
constexpr uint32_t DamageBurn = 1u << 3;
constexpr uint32_t DamageFall = 1u << 5;
constexpr uint32_t DamageBlast = 1u << 6;
constexpr uint32_t DamageClub = 1u << 7;
constexpr uint32_t DamageShock = 1u << 8;
constexpr uint32_t DamageHeadshot = 1u << 19;
/** @} */

/** Who deals the damage, how much, and of which kind. */
struct DamageInfo
{
    EntityRef Attacker;   ///< credited in the kill feed and `player_death`
    EntityRef Inflictor;  ///< what did it, such as a grenade or a turret; empty means the attacker
    float Amount = 0.0f;
    uint32_t Type = DamageGeneric;  ///< @ref DamageTypes bits
};

/** One hit on its way to the engine, as a @ref Damage::Before handler sees it. */
struct DamageHit
{
    Entity Victim;
    /** Edits to Amount and Type reach the engine; Attacker and Inflictor are read-only here. */
    DamageInfo Info;
    /** Set to cancel the hit; nothing is dealt and no damage event fires. */
    bool Blocked = false;
};

/**
 * @brief Every entity's damage, from `CBaseEntity::TakeDamageOld`, and a way to deal it.
 *
 * The hook sits where the function starts, so it sees players, props and anything else that takes
 * damage, including hits from @ref Apply. It installs on the first `Before` subscription.
 *
 * @code
 * _damage = runtime.Hooks.Damage.Before += [this](VoltMod::DamageHit& hit) {
 *     if (IsStructure(hit.Victim.Ref()))
 *         hit.Blocked = HitStructure(hit.Victim.Ref(), hit.Info);
 * };
 * runtime.Hooks.Damage.Apply(bot, {.Attacker = owner.Ref(), .Inflictor = turret, .Amount = 25,
 *                                  .Type = VoltMod::DamageBullet});
 * @endcode
 *
 * Game-thread only.
 */
class Damage
{
public:
    /** @p entities resolves the refs and @p bindings supplies the two damage functions. Both must
     *  outlive this service; the Runtime declares them above. */
    Damage(EntitySystem& entities, const Bindings& bindings);
    ~Damage();
    Damage(const Damage&) = delete;
    Damage& operator=(const Damage&) = delete;

    /** Raised before the engine applies a hit. */
    Event<DamageHit&> Before;

    /** Why damage cannot be hooked or dealt: a damage signature did not bind. */
    Status Available() const;

    /**
     * Deal @p info to @p victim through the engine, so death, the kill feed and `player_death`
     * credit @p info's attacker as if its own weapon had hit. Does nothing for a falsy victim or
     * while @ref Available fails.
     */
    void Apply(const Entity& victim, const DamageInfo& info) const;

private:
    bool Install();
    HookResult<int64_t> OnTakeDamage(CEntityInstance& victim, void* info);

    EntitySystem& _entities;
    const Bindings& _bindings;
    Subscription _hook;
};

}  // namespace VoltMod
