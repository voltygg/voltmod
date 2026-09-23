#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Engine/Color.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/EntityRef.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace VoltMod
{

/** Glow colors and an optional per-slot veto. */
struct GlowConfig
{
    Color TerroristColor{255, 128, 0};
    Color CtColor{0, 160, 255};
    /** Extra per-slot veto on top of the built-in live/team/visibility checks (empty = all). */
    std::function<bool(int slot)> Filter;
};

/**
 * @brief Control which clients receive entities in the CheckTransmit hook.
 *
 * Pawn hiding removes the pawn, weapons, wearables, gloves, and shadow from other clients while
 * preserving an observer's camera. Controller hiding removes the player's scoreboard row and
 * prevents those clients from attributing the player's chat or voice.
 *
 * @ref ShowOnlyTo sends an entity to one client and clears it for all others. Entries use
 * @ref EntityRef, so removing an entity cannot affect a later occupant of the same index. Sounds
 * are networked separately and are unaffected.
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

    /** Apply the visibility filter after ISource2GameEntities::CheckTransmit. */
    void OnCheckTransmit(CCheckTransmitInfo** infoList, int infoCount);

    /** Why the filter cannot run: the CheckTransmitPlayerSlot offset did not bind. Every call above
     *  is then accepted but inert. */
    Status Available() const
    {
        if (!_bindings.VisibilityRecipientSlot)
        {
            return std::unexpected(Error::Unsupported("the CheckTransmitPlayerSlot offset did not bind"));
        }
        return {};
    }

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
    /** Declared last so it unregisters before its callback targets are destroyed. */
    Subscription _slotListener;
};

}  // namespace VoltMod
