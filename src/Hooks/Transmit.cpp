#include "Entities/Schema.hpp"

#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Hooks/Transmit.hpp>
#include <checktransmitinfo.h>
#include <cstdint>
#include <entity2/entityinstance.h>

namespace VoltMod
{

// CNetworkUtlVectorBase<CHandle<T>>: element count at +0, element pointer at +8.
// Only these two fields are read; the vector is never mutated.
struct HandleVectorView
{
    int32_t Count;
    int32_t _pad;
    const uint32_t* Elements;
};

// Pawn + 12 weapon slots + a handful of wearables is well within this.
static constexpr int MaxIndicesPerPlayer = 24;

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

static void AddHandleVector(EntitySystem& entities, HiddenPlayer& player, void* base, int offset)
{
    if (offset < 0)
        return;
    const auto* view = MemberPtr<const HandleVectorView>(base, offset);
    if (!view->Elements)
        return;
    for (int32_t i = 0; i < view->Count && i < MaxIndicesPerPlayer; ++i)
        AddIndex(player, entities.GetEntityIndex(entities.ResolveEntityHandle(view->Elements[i])));
}

static CEntityInstance* GetCurrentPawn(EntitySystem& entities, SchemaService& schema, int slot)
{
    auto* controller = entities.GetPlayerController(slot);
    if (!controller)
        return nullptr;
    // m_hPawn is the possessed pawn (observer pawn while dead/spectating), unlike m_hPlayerPawn.
    int offset = schema.GetOffsetOf<uint32_t>("CBasePlayerController", "m_hPawn");
    if (offset < 0)
        return nullptr;
    return entities.ResolveEntityHandle(ReadAt<uint32_t>(controller, offset));
}

// What `recipientSlot` is currently spectating, or nullptr. An observed pawn must keep
// transmitting to that client or its spectator camera breaks. The answer depends only on the
// recipient, so the caller resolves it once per recipient rather than once per hidden pawn.
static CEntityInstance* GetObserverTarget(EntitySystem& entities, SchemaService& schema, int recipientSlot)
{
    auto* pawn = GetCurrentPawn(entities, schema, recipientSlot);
    if (!pawn)
        return nullptr;

    int servicesOffset = schema.GetOffsetOf<void*>("CBasePlayerPawn", "m_pObserverServices");
    if (servicesOffset < 0)
        return nullptr;
    auto* observerServices = ReadAt<void*>(pawn, servicesOffset);
    if (!observerServices)
        return nullptr;

    int targetOffset = schema.GetOffsetOf<uint32_t>("CPlayer_ObserverServices", "m_hObserverTarget");
    if (targetOffset < 0)
        return nullptr;
    return entities.ResolveEntityHandle(ReadAt<uint32_t>(observerServices, targetOffset));
}

static void CollectHiddenPlayer(EntitySystem& entities, SchemaService& schema, int slot, bool pawnHidden,
                                bool controllerHidden, HiddenPlayer& out)
{
    out.Slot = slot;

    auto* controller = entities.GetPlayerController(slot);
    if (!controller)
        return;

    if (controllerHidden)
        out.ControllerIndex = entities.GetEntityIndex(controller);

    if (!pawnHidden)
        return;

    int pawnOffset = schema.GetOffsetOf<uint32_t>("CCSPlayerController", "m_hPlayerPawn");
    if (pawnOffset < 0)
        return;
    out.Pawn = entities.ResolveEntityHandle(ReadAt<uint32_t>(controller, pawnOffset));
    if (!out.Pawn)
        return;

    AddIndex(out, entities.GetEntityIndex(out.Pawn));

    int weaponServicesOffset = schema.GetOffsetOf<void*>("CBasePlayerPawn", "m_pWeaponServices");
    if (weaponServicesOffset >= 0)
    {
        if (auto* weaponServices = ReadAt<void*>(out.Pawn, weaponServicesOffset))
            AddHandleVector(entities, out, weaponServices, schema.GetOffset("CPlayer_WeaponServices", "m_hMyWeapons"));
    }

    AddHandleVector(entities, out, out.Pawn, schema.GetOffset("CBaseCombatCharacter", "m_hMyWearables"));
}

Transmit::Transmit(EntitySystem& entities, const Bindings& bindings, SchemaService& schema, SlotEvents& slots)
    : _entities(entities), _bindings(bindings), _schema(schema)
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
            CollectHiddenPlayer(_entities, _schema, slot, state.PawnHidden, state.ControllerHidden,
                                hidden[hiddenCount++]);
    }

    for (int i = 0; i < infoCount; ++i)
    {
        auto* info = infoList[i];
        if (!info || !info->m_pTransmitEntity)
            continue;

        int recipient = static_cast<int>(_bindings.CheckTransmitPlayerSlot.Read(info));
        CEntityInstance* observed = hiddenCount > 0 ? GetObserverTarget(_entities, _schema, recipient) : nullptr;

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
