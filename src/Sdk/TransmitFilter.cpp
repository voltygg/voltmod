#include "Sdk/Schema.hpp"

#include <CS2Kit/Sdk/Entity.hpp>
#include <CS2Kit/Sdk/GameData.hpp>
#include <CS2Kit/Sdk/MemoryAccess.hpp>
#include <CS2Kit/Sdk/SdkServices.hpp>
#include <CS2Kit/Sdk/TransmitFilter.hpp>
#include <checktransmitinfo.h>
#include <cstdint>
#include <entity2/entityinstance.h>

namespace CS2Kit::Sdk
{

namespace
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
constexpr int MaxIndicesPerPlayer = 24;

struct HiddenPlayer
{
    int Slot = -1;
    int ControllerIndex = -1;
    CEntityInstance* Pawn = nullptr;
    int IndexCount = 0;
    std::array<int, MaxIndicesPerPlayer> PawnIndices{};  // pawn itself + weapons + wearables
};

void AddIndex(HiddenPlayer& player, int index)
{
    if (index > 0 && player.IndexCount < MaxIndicesPerPlayer)
        player.PawnIndices[player.IndexCount++] = index;
}

void AddHandleVector(HiddenPlayer& player, void* base, int offset)
{
    if (offset < 0)
        return;
    const auto* view = MemberPtr<const HandleVectorView>(base, offset);
    if (!view->Elements)
        return;
    auto& entities = Ctx().Entities;
    for (int32_t i = 0; i < view->Count && i < MaxIndicesPerPlayer; ++i)
        AddIndex(player, entities.GetEntityIndex(entities.ResolveEntityHandle(view->Elements[i])));
}

CEntityInstance* GetCurrentPawn(int slot)
{
    auto* controller = Ctx().Entities.GetPlayerController(slot);
    if (!controller)
        return nullptr;
    // m_hPawn is the possessed pawn (observer pawn while dead/spectating), unlike m_hPlayerPawn.
    int offset = Ctx().Schema().GetOffset("CBasePlayerController", "m_hPawn", sizeof(uint32_t));
    if (offset < 0)
        return nullptr;
    return Ctx().Entities.ResolveEntityHandle(ReadAt<uint32_t>(controller, offset));
}

// True when `recipientSlot` is currently observing `hiddenPawn`; the pawn must keep
// transmitting to that client or its spectator camera breaks.
bool IsObservingPawn(int recipientSlot, CEntityInstance* hiddenPawn)
{
    auto* pawn = GetCurrentPawn(recipientSlot);
    if (!pawn)
        return false;

    auto& schema = Ctx().Schema();
    int servicesOffset = schema.GetOffset("CBasePlayerPawn", "m_pObserverServices");
    if (servicesOffset < 0)
        return false;
    auto* observerServices = ReadAt<void*>(pawn, servicesOffset);
    if (!observerServices)
        return false;

    int targetOffset = schema.GetOffset("CPlayer_ObserverServices", "m_hObserverTarget");
    if (targetOffset < 0)
        return false;
    auto targetHandle = ReadAt<uint32_t>(observerServices, targetOffset);
    return Ctx().Entities.ResolveEntityHandle(targetHandle) == hiddenPawn;
}

void CollectHiddenPlayer(int slot, bool pawnHidden, bool controllerHidden, HiddenPlayer& out)
{
    out.Slot = slot;

    auto* controller = Ctx().Entities.GetPlayerController(slot);
    if (!controller)
        return;

    if (controllerHidden)
        out.ControllerIndex = Ctx().Entities.GetEntityIndex(controller);

    if (!pawnHidden)
        return;

    auto& schema = Ctx().Schema();
    int pawnOffset = schema.GetOffset("CCSPlayerController", "m_hPlayerPawn");
    if (pawnOffset < 0)
        return;
    out.Pawn = Ctx().Entities.ResolveEntityHandle(ReadAt<uint32_t>(controller, pawnOffset));
    if (!out.Pawn)
        return;

    AddIndex(out, Ctx().Entities.GetEntityIndex(out.Pawn));

    int weaponServicesOffset = schema.GetOffset("CBasePlayerPawn", "m_pWeaponServices");
    if (weaponServicesOffset >= 0)
    {
        if (auto* weaponServices = ReadAt<void*>(out.Pawn, weaponServicesOffset))
            AddHandleVector(out, weaponServices, schema.GetOffset("CPlayer_WeaponServices", "m_hMyWeapons"));
    }

    AddHandleVector(out, out.Pawn, schema.GetOffset("CBaseCombatCharacter", "m_hMyWearables"));
}

}  // namespace

bool TransmitFilterService::Initialize()
{
    _slotOffset = Ctx().GameData.GetOffset("CheckTransmitPlayerSlot");
    return _slotOffset >= 0;
}

void TransmitFilterService::SetFlag(int slot, bool SlotState::* flag, bool value)
{
    if (!Core::IsValidSlot(slot))
        return;

    auto& state = _state[slot];
    bool wasActive = state.PawnHidden || state.ControllerHidden;
    state.*flag = value;
    bool isActive = state.PawnHidden || state.ControllerHidden;
    _activeCount += static_cast<int>(isActive) - static_cast<int>(wasActive);
}

void TransmitFilterService::SetPawnHidden(int slot, bool hidden)
{
    SetFlag(slot, &SlotState::PawnHidden, hidden);
}

void TransmitFilterService::SetControllerHidden(int slot, bool hidden)
{
    SetFlag(slot, &SlotState::ControllerHidden, hidden);
}

bool TransmitFilterService::IsPawnHidden(int slot) const
{
    return Core::IsValidSlot(slot) && _state[slot].PawnHidden;
}

bool TransmitFilterService::IsControllerHidden(int slot) const
{
    return Core::IsValidSlot(slot) && _state[slot].ControllerHidden;
}

void TransmitFilterService::SetEntityExclusive(int entityIndex, int beneficiarySlot)
{
    if (entityIndex <= 0 || !Core::IsValidSlot(beneficiarySlot))
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

void TransmitFilterService::ClearEntityExclusive(int entityIndex)
{
    std::erase_if(_exclusive, [entityIndex](const ExclusiveEntity& e) { return e.EntityIndex == entityIndex; });
}

void TransmitFilterService::OnPlayerDisconnect(int slot)
{
    SetPawnHidden(slot, false);
    SetControllerHidden(slot, false);
    // The owning effect normally cleans up first (effect cancel runs before this);
    // this catches entries whose beneficiary vanished without cleanup.
    std::erase_if(_exclusive, [slot](const ExclusiveEntity& e) { return e.BeneficiarySlot == slot; });
}

void TransmitFilterService::OnCheckTransmit(CCheckTransmitInfo** infoList, int infoCount)
{
    if ((_activeCount == 0 && _exclusive.empty()) || _slotOffset < 0 || !infoList)
        return;

    // Entity indices are the same for every recipient (only the self/observer
    // exemptions differ per client), so gather them once per snapshot.
    std::array<HiddenPlayer, Core::MaxPlayers> hidden;
    int hiddenCount = 0;
    for (int slot = 0; slot < Core::MaxPlayers && hiddenCount < _activeCount; ++slot)
    {
        const auto& state = _state[slot];
        if (state.PawnHidden || state.ControllerHidden)
            CollectHiddenPlayer(slot, state.PawnHidden, state.ControllerHidden, hidden[hiddenCount++]);
    }

    for (int i = 0; i < infoCount; ++i)
    {
        auto* info = infoList[i];
        if (!info || !info->m_pTransmitEntity)
            continue;

        int recipient = static_cast<int>(ReadAt<uint8_t>(info, _slotOffset));

        for (int h = 0; h < hiddenCount; ++h)
        {
            const auto& player = hidden[h];
            if (player.Slot == recipient)
                continue;

            if (player.IndexCount > 0 && !IsObservingPawn(recipient, player.Pawn))
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

}  // namespace CS2Kit::Sdk
