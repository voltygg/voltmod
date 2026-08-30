#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/Transmit.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <checktransmitinfo.h>
#include <cstdint>
#include <entity2/entityinstance.h>

namespace VoltMod
{

// Pawn + 12 weapon slots + a handful of wearables is well within this.
static constexpr int MaxIndicesPerPlayer = 24;

// CNetworkUtlVectorBase<CHandle<T>>: element count at +0, element pointer at +8.
// Only these two fields are read; the vector is never mutated.
struct HandleVectorView
{
    int32_t Count;
    int32_t _pad;
    const uint32_t* Elements;
};

struct HiddenPlayer
{
    int Slot = -1;
    int ControllerIndex = -1;
    CEntityInstance* Pawn = nullptr;
    int IndexCount = 0;
    std::array<int, MaxIndicesPerPlayer> PawnIndices{};  // pawn itself + weapons + wearables
};

static void AddIndex(HiddenPlayer& player, int index)
{
    if (index > 0 && player.IndexCount < MaxIndicesPerPlayer)
        player.PawnIndices[player.IndexCount++] = index;
}

// The generated accessor bakes the vector's offset and hands back its address; the element
// layout stays here, where the one struct that describes it lives.
static void AddHandleVector(EntitySystem& entities, HiddenPlayer& player, void* vector)
{
    const auto* view = static_cast<const HandleVectorView*>(vector);
    if (!view || !view->Elements)
        return;
    for (int32_t i = 0; i < view->Count && i < MaxIndicesPerPlayer; ++i)
        AddIndex(player, entities.Resolve(EntityRef{view->Elements[i]}).Index());
}

// What `recipientSlot` is currently spectating, or nullptr. An observed pawn must keep
// transmitting to that client or its spectator camera breaks. The answer depends only on the
// recipient, so the caller resolves it once per recipient rather than once per hidden pawn.
static CEntityInstance* GetObserverTarget(EntitySystem& entities, int recipientSlot)
{
    // Possessed(), not GetPawn(): while dead or spectating the observer pawn carries the camera.
    Pawn pawn = entities.Controller(recipientSlot).Possessed();
    const Schema::CPlayer_ObserverServices services = pawn.ObserverServices();
    if (!services)
        return nullptr;

    return entities.Resolve(EntityRef{services.ObserverTarget()}).Raw();
}

static void CollectHiddenPlayer(EntitySystem& entities, int slot, bool pawnHidden, bool controllerHidden,
                                HiddenPlayer& out)
{
    out.Slot = slot;

    Controller controller = entities.Controller(slot);
    if (!controller)
        return;

    if (controllerHidden)
        out.ControllerIndex = controller.Index();

    if (!pawnHidden)
        return;

    Pawn pawn = controller.GetPawn();
    if (!pawn)
        return;

    out.Pawn = pawn.Raw();
    AddIndex(out, pawn.Index());

    AddHandleVector(entities, out, pawn.WeaponServices().MyWeapons());
    AddHandleVector(entities, out, pawn.MyWearables());
}

Transmit::Transmit(EntitySystem& entities, const Bindings& bindings, SlotEvents& slots)
    : _entities(entities), _bindings(bindings)
{
    // SlotEvents fires when a slot is filled as well as emptied; a fresh occupant has nothing
    // hidden, so clearing on both edges covers "left" without a dedicated event.
    _slotListener = slots.Changed += [this](int slot) {
        SetPawnHidden(slot, false);
        SetControllerHidden(slot, false);
        // The owning effect normally cleans up first (effect cancel runs before this);
        // this catches entries whose beneficiary vanished without cleanup.
        std::erase_if(_exclusive, [slot](const ExclusiveEntity& e) { return e.BeneficiarySlot == slot; });
    };
}

void Transmit::SetFlag(int slot, bool SlotState::* flag, bool value)
{
    if (!IsValidSlot(slot))
        return;

    auto& state = _state[slot];
    bool wasActive = state.PawnHidden || state.ControllerHidden;
    state.*flag = value;
    bool isActive = state.PawnHidden || state.ControllerHidden;
    _activeCount += static_cast<int>(isActive) - static_cast<int>(wasActive);
}

void Transmit::SetPawnHidden(int slot, bool hidden)
{
    SetFlag(slot, &SlotState::PawnHidden, hidden);
}

void Transmit::SetControllerHidden(int slot, bool hidden)
{
    SetFlag(slot, &SlotState::ControllerHidden, hidden);
}

bool Transmit::IsPawnHidden(int slot) const
{
    return IsValidSlot(slot) && _state[slot].PawnHidden;
}

bool Transmit::IsControllerHidden(int slot) const
{
    return IsValidSlot(slot) && _state[slot].ControllerHidden;
}

void Transmit::SetEntityExclusive(int entityIndex, int beneficiarySlot)
{
    if (entityIndex <= 0 || !IsValidSlot(beneficiarySlot))
        return;

    for (auto& entry : _exclusive)
    {
        if (entry.EntityIndex == entityIndex)
        {
            entry.BeneficiarySlot = beneficiarySlot;
            return;
        }
    }
    _exclusive.push_back({entityIndex, beneficiarySlot});
}

void Transmit::ClearEntityExclusive(int entityIndex)
{
    std::erase_if(_exclusive, [entityIndex](const ExclusiveEntity& e) { return e.EntityIndex == entityIndex; });
}

void Transmit::OnCheckTransmit(CCheckTransmitInfo** infoList, int infoCount)
{
    if ((_activeCount == 0 && _exclusive.empty()) || !_bindings.CheckTransmitPlayerSlot || !infoList)
        return;

    // Entity indices are the same for every recipient (only the self/observer
    // exemptions differ per client), so gather them once per snapshot.
    std::array<HiddenPlayer, MaxPlayers> hidden;
    int hiddenCount = 0;
    for (int slot = 0; slot < MaxPlayers && hiddenCount < _activeCount; ++slot)
    {
        const auto& state = _state[slot];
        if (state.PawnHidden || state.ControllerHidden)
            CollectHiddenPlayer(_entities, slot, state.PawnHidden, state.ControllerHidden, hidden[hiddenCount++]);
    }

    for (int i = 0; i < infoCount; ++i)
    {
        auto* info = infoList[i];
        if (!info || !info->m_pTransmitEntity)
            continue;

        int recipient = static_cast<int>(_bindings.CheckTransmitPlayerSlot.Read(info));
        CEntityInstance* observed = hiddenCount > 0 ? GetObserverTarget(_entities, recipient) : nullptr;

        for (int h = 0; h < hiddenCount; ++h)
        {
            const auto& player = hidden[h];
            if (player.Slot == recipient)
                continue;

            if (player.IndexCount > 0 && observed != player.Pawn)
            {
                for (int n = 0; n < player.IndexCount; ++n)
                    info->m_pTransmitEntity->Clear(player.PawnIndices[n]);
            }

            if (player.ControllerIndex > 0)
                info->m_pTransmitEntity->Clear(player.ControllerIndex);
        }

        for (const auto& entry : _exclusive)
        {
            if (entry.BeneficiarySlot != recipient)
                info->m_pTransmitEntity->Clear(entry.EntityIndex);
        }
    }
}

}  // namespace VoltMod
