#pragma once

#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <cstdint>
#include <functional>

namespace VoltMod::Sdk
{

class EntitySystem;
class GameData;

/** Engine hitgroup ids, as carried by CTakeDamageInfo::m_iHitGroupId. */
enum class HitGroup : int
{
    Invalid = -1,
    Generic = 0,
    Head = 1,
    Chest = 2,
    Stomach = 3,
    LeftArm = 4,
    RightArm = 5,
    LeftLeg = 6,
    RightLeg = 7,
    Neck = 8,
};

/** One incoming damage event, as a listener sees it. */
struct DamageView
{
    int VictimSlot = -1;   /**< -1 when the victim is not a player pawn */
    int AttackerSlot = -1; /**< -1 for world damage (fall, fire, the bomb) */
    HitGroup Hitbox = HitGroup::Generic;
    uint32_t DamageTypes = 0; /**< DMG_* bits from the originating info */
    float Damage = 0.0f;      /**< writable: what the victim will actually lose */
    bool Suppress = false;    /**< set to cancel the damage entirely */
};

/**
 * @brief Manual vtable hook on CCSPlayerPawn::OnTakeDamage_Alive - every point of damage a
 * living player takes (gamedata offset "OnTakeDamageAlive").
 *
 * Listeners run before the engine applies the damage and may rewrite @ref DamageView::Damage or
 * set @ref DamageView::Suppress, which is what makes headshot-only rules, damage multipliers and
 * one-hit-kill modes possible. Suppression supersedes the engine call outright; a rewritten
 * amount is written back into the result the engine goes on to use.
 *
 * Like MovementHook this binds the class vtable (located by RTTI on Windows, ELF symbol on
 * Linux), so Install() works from OnLoad with no player connected and covers every pawn from
 * then on. It is dormant until Install().
 *
 * The vtable index and the CTakeDamageInfo/CTakeDamageResult field offsets are
 * gamedata-maintained and drift with CS2 updates. A wrong index calls an unrelated vfunc and
 * crashes, so re-verify after every update; a missing index leaves the hook uninstalled and
 * every listener silent rather than guessing.
 */
class DamageHook
{
public:
    using Callback = std::function<void(DamageView&)>;

    /** @p entities resolves pawns to slots, @p gameData the vtable index and field offsets.
     *  Both must outlive this hook; the Runtime declares them above it. */
    DamageHook(EntitySystem& entities, GameData& gameData) : _entities(entities), _gameData(gameData) {}
    ~DamageHook() { Remove(); }
    DamageHook(const DamageHook&) = delete;
    DamageHook& operator=(const DamageHook&) = delete;

    /**
     * Install the class-vtable hook; safe from OnLoad and a no-op once installed.
     * @return false when the gamedata index or the pawn class cannot be resolved.
     */
    bool Install();
    void Remove();
    bool Installed() const { return _installed; }

    /** Called for each damage event, in registration order. Store the subscription beside the
     *  state the callback captures. */
    [[nodiscard]] Core::Subscription ListenPreDamage(Callback callback)
    {
        return _listeners.AddOwned(std::move(callback));
    }

private:
    bool Hook_OnTakeDamageAlive(void* result);

    /** Slot whose pawn is @p pawn, or -1. */
    int SlotFromPawn(void* pawn) const;

    EntitySystem& _entities;
    GameData& _gameData;
    Core::CallbackRegistry<Callback> _listeners;
    bool _installed = false;
    int _hookId = 0;
};

}  // namespace VoltMod::Sdk
