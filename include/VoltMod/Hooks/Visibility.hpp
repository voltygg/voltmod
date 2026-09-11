#pragma once

#include <Color.h>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace VoltMod
{

/** Colors and the optional per-slot veto for a @ref GlowVision. */
struct GlowConfig
{
    Color TerroristColor{255, 128, 0, 255};
    Color CtColor{0, 160, 255, 255};
    /** Extra per-slot veto on top of the built-in live/team/visibility checks (empty = all). */
    std::function<bool(int slot)> Filter;
};

/**
 * @brief Who receives which entities, decided per client in the CheckTransmit hook.
 *
 * Hiding a player here stops the server from networking the chosen entities to other clients
 * entirely. Unlike render-alpha tricks, the model, its weapons, wearables, gloves and shadow all
 * disappear, because the client never receives the entities at all. Two independent toggles per
 * slot:
 *
 * - Pawn hiding removes the pawn plus its weapons and wearables from everyone except the player
 *   themself and any client currently observing that pawn (dropping the pawn mid-spectate would
 *   break the observer's camera).
 * - Controller hiding removes the player's CCSPlayerController, which removes their row from the
 *   scoreboard. Side effect: clients cannot attribute chat or voice from a player whose controller
 *   they never received.
 *
 * @ref ShowOnlyTo is the inverse: an entity networked to one client and cleared from every other
 * (per-viewer effects like glow clones and private HUD panels). Entries are keyed by
 * @ref EntityRef, so one whose entity is gone drops itself rather than filtering whatever entity
 * is handed that index next.
 *
 * Sounds (footsteps, gunfire) are networked separately and are not affected.
 */
class Visibility
{
public:
    /** @p slots tells the service when a slot changes hands, so hiding cannot carry over to
     *  whoever occupies it next. All four must outlive it; the Runtime declares them above. */
    Visibility(EntitySystem& entities, const Bindings& bindings, SlotEvents& slots, EntityOps& ops);
    Visibility(const Visibility&) = delete;
    Visibility& operator=(const Visibility&) = delete;

    /** Hide/show `slot`'s pawn (plus its weapons and wearables) from every other client. */
    void SetPawnHidden(int slot, bool hidden);

    /** Hide/show `slot`'s controller from every other client (removes the scoreboard row). */
    void SetControllerHidden(int slot, bool hidden);

    bool IsPawnHidden(int slot) const;
    bool IsControllerHidden(int slot) const;

    /** Network `entity` to `slot` alone. Calling again moves it to another slot. */
    void ShowOnlyTo(EntityRef entity, int slot);

    /** Undo @ref ShowOnlyTo. Safe on unknown refs, and not needed for an entity that is being
     *  removed: its entry goes with it. */
    void ShowToEveryone(EntityRef entity);

    /** A @ref GlowVision for @p viewerSlot. Shared because the usual driver is a repeating tick
     *  that captures it; call @ref GlowVision::Destroy before dropping the last owner. */
    std::shared_ptr<GlowVision> CreateGlow(int viewerSlot, GlowConfig config = {});

    /** Post-hook body for ISource2GameEntities::CheckTransmit; called by MetamodPlugin. */
    void OnCheckTransmit(CCheckTransmitInfo** infoList, int infoCount);

    /** Whether the filter runs. False means the gamedata offset is missing and every call above
     *  is accepted but inert. */
    [[nodiscard]] bool IsActive() const noexcept { return static_cast<bool>(_bindings.CheckTransmitPlayerSlot); }

private:
    struct SlotState
    {
        bool PawnHidden = false;
        bool ControllerHidden = false;

        bool Any() const { return PawnHidden || ControllerHidden; }
    };

    struct PrivateEntity
    {
        EntityRef Entity;
        int Viewer;
        int Index = -1;  ///< resolved once per snapshot by OnCheckTransmit
    };

    EntitySystem& _entities;
    const Bindings& _bindings;
    EntityOps& _ops;
    std::array<SlotState, MaxPlayers> _state{};
    std::vector<PrivateEntity> _private;
    /** Declared after the state above so it unregisters before its callback's targets go away. */
    Subscription _slotListener;
};

}  // namespace VoltMod
