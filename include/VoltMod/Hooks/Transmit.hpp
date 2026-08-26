#pragma once

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <array>
#include <vector>

class CCheckTransmitInfo;

namespace VoltMod::Engine
{
class GameData;
}  // namespace VoltMod::Engine

namespace VoltMod::Entities
{
class EntitySystem;
}  // namespace VoltMod::Entities

namespace VoltMod::Entities
{
class SchemaService;  // Internal type (src/Entities/Schema.hpp), kept out of the public graph.
}  // namespace VoltMod::Entities

namespace VoltMod::Hooks
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
    Transmit(Entities::EntitySystem& entities, Engine::GameData& gameData, Entities::SchemaService& schema,
             Core::SlotEvents& slots);
    Transmit(const Transmit&) = delete;
    Transmit& operator=(const Transmit&) = delete;

    /** Cache the CCheckTransmitInfo recipient-slot gamedata offset. False leaves the service inert. */
    bool Initialize();

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

    Entities::EntitySystem& _entities;
    Engine::GameData& _gameData;
    Entities::SchemaService& _schema;
    std::array<SlotState, Core::MaxPlayers> _state{};
    std::vector<ExclusiveEntity> _exclusive; /**< Entities transmitted only to their beneficiary. */
    int _activeCount = 0;                    /**< Slots with any flag set; OnCheckTransmit early-outs at 0. */
    int _slotOffset = -1;                    /**< Recipient player-slot byte offset inside CCheckTransmitInfo. */
    /** Declared after the state above so it unregisters before its callback's targets go away. */
    Core::Subscription _slotListener;
};

}  // namespace VoltMod::Hooks
