#pragma once

#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/HitGroup.hpp>
#include <cstdint>
#include <functional>

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
 * Like Movement this binds the class vtable (RTTI on Windows, ELF symbol on Linux), so
 * Install() works from OnLoad with no player connected and covers every pawn from then on.
 *
 * The vtable index and every field offset are gamedata-maintained and drift with CS2 updates. A
 * wrong index calls an unrelated vfunc and crashes, so re-verify after every update; anything that
 * fails to resolve leaves the hook uninstalled and every listener silent rather than guessing.
 */
class Damage
{
public:
    using Callback = std::function<void(const DamageView&)>;

    /** Both must outlive this hook; the Runtime declares them above it. */
    Damage(EntitySystem& entities, GameData& gameData) : _entities(entities), _gameData(gameData) {}
    ~Damage() { Remove(); }
    Damage(const Damage&) = delete;
    Damage& operator=(const Damage&) = delete;

    /** Install the class-vtable hook; safe from OnLoad and a no-op once installed.
     *  @return false when the gamedata index or the pawn class cannot be resolved. */
    bool Install();
    void Remove();
    bool Installed() const { return _installed; }

    /** Called for each damage event, in registration order. Store the subscription beside the
     *  state the callback captures. */
    [[nodiscard]] Subscription Listen(Callback callback) { return _listeners.AddOwned(std::move(callback)); }

private:
    bool Hook_OnTakeDamageAlive(void* result);

    /** Walk info -> trace -> hitbox for the struck hitgroup. */
    HitGroup ReadHitGroup(void* info) const;

    /** Read every damage field offset from gamedata. False when one is missing or implausible. */
    bool ResolveOffsets();

    EntitySystem& _entities;
    GameData& _gameData;
    CallbackRegistry<Callback> _listeners;
    // CTakeDamageInfo fields, then the trace chain the hitgroup lives on. Resolved once by
    // Install(). Adding to this set grows Runtime, which holds the hook by value - see the
    // crash-triage skill on the latent out-of-bounds write that some sizes turn fatal.
    int _offsetAttacker = -1;
    int _offsetDamage = -1;
    int _offsetDamageTypes = -1;
    int _offsetTrace = -1;
    int _offsetTraceHitbox = -1;
    int _offsetHitboxGroup = -1;
    bool _installed = false;
    int _hookId = 0;
};

}  // namespace VoltMod
