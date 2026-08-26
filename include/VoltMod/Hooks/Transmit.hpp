#pragma once

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <array>
#include <vector>

namespace VoltMod
{

/**
 * @brief Per-recipient entity transmit filtering, backed by the CheckTransmit hook.
 *
 * Hiding a player here stops the server from networking the chosen entities to
 * other clients entirely. Unlike render-alpha tricks, the model, its weapons,
 * wearables, gloves and shadow all disappear, because the client never receives
 * the entities at all. Two independent toggles per slot:
 *
 * - Pawn hiding removes the pawn plus its weapons and wearables from everyone
 *   except the player themself and any client currently observing that pawn
 *   (dropping the pawn mid-spectate would break the observer's camera).
 * - Controller hiding removes the player's CCSPlayerController, which removes
 *   their row from the scoreboard. Side effect: clients cannot attribute chat
 *   or voice from a player whose controller they never received.
 *
 * Exclusive entities are the inverse: an entity transmits only to its beneficiary
 * slot, cleared from every other recipient (per-viewer effects like glow clones).
 *
 * Sounds (footsteps, gunfire) are networked separately and are not affected.
 */
class Transmit
{
public:
    /** @p slots tells the service when a slot changes hands, so hiding cannot carry over to
     *  whoever occupies it next. The other three must outlive it; the Runtime declares them above. */
    Transmit(EntitySystem& entities, const Bindings& bindings, SchemaService& schema, SlotEvents& slots);
    Transmit(const Transmit&) = delete;
    Transmit& operator=(const Transmit&) = delete;

    /** Hide/show `slot`'s pawn (plus its weapons and wearables) from every other client. */
    void SetPawnHidden(int slot, bool hidden);

    /** Hide/show `slot`'s controller from every other client (removes the scoreboard row). */
    void SetControllerHidden(int slot, bool hidden);

    bool IsPawnHidden(int slot) const;
    bool IsControllerHidden(int slot) const;

    /** Transmit `entityIndex` only to `beneficiarySlot`. Re-registering updates the beneficiary. */
    void SetEntityExclusive(int entityIndex, int beneficiarySlot);

    /** Stop filtering `entityIndex`; it transmits normally again. Safe on unknown indices. */
    void ClearEntityExclusive(int entityIndex);

    /** Post-hook body for ISource2GameEntities::CheckTransmit; called by MetamodPlugin. */
    void OnCheckTransmit(CCheckTransmitInfo** infoList, int infoCount);

private:
    struct SlotState
    {
        bool PawnHidden = false;
        bool ControllerHidden = false;
    };

    struct ExclusiveEntity
    {
        int EntityIndex;
        int BeneficiarySlot;
    };

    void SetFlag(int slot, bool SlotState::* flag, bool value);

    EntitySystem& _entities;
    const Bindings& _bindings;
    SchemaService& _schema;
    std::array<SlotState, MaxPlayers> _state{};
    std::vector<ExclusiveEntity> _exclusive; /**< Entities transmitted only to their beneficiary. */
    int _activeCount = 0;                    /**< Slots with any flag set; OnCheckTransmit early-outs at 0. */
    /** Declared after the state above so it unregisters before its callback's targets go away. */
    Subscription _slotListener;
};

}  // namespace VoltMod
