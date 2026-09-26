#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/GlowVision.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <algorithm>
#include <checktransmitinfo.h>
#include <cstdint>
#include <entity2/entityinstance.h>
#include <entityhandle.h>
#include <tier1/utlvector.h>
#include <utility>

namespace VoltMod
{

// The fixed capacity covers the pawn, weapons, and wearables.
static constexpr int MaxIndicesPerPlayer = 24;

// The schema's CNetworkUtlVectorBase<CHandle<T>> fields are 24 bytes, laid out as a CUtlVector.
using HandleVector = CUtlVector<CEntityHandle>;
static_assert(sizeof(HandleVector) == 24);

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
    {
        player.PawnIndices[player.IndexCount++] = index;
    }
}

static void AddHandleVector(EntitySystem& entities, HiddenPlayer& player, const void* vector)
{
    const auto* handles = static_cast<const HandleVector*>(vector);
    if (!handles)
    {
        return;
    }
    for (int i = 0; i < handles->Count() && i < MaxIndicesPerPlayer; ++i)
    {
        AddIndex(player, entities.Get(EntityRef{static_cast<uint32_t>(handles->Element(i).ToInt())}).Index());
    }
}

// Return the pawn watched by `recipientSlot`. It must remain transmissible to preserve spectator view.
static CEntityInstance* ObserverTarget(EntitySystem& entities, int recipientSlot)
{
    // The observer pawn carries the camera while dead or spectating.
    Pawn pawn = entities.Controller(recipientSlot).InputPawn();
    const Schema::CPlayer_ObserverServices services = pawn.ObserverServices();
    if (!services)
    {
        return nullptr;
    }

    return entities.Get(services.ObserverTargetRef()).Raw();
}

static void CollectHiddenPlayer(EntitySystem& entities, int slot, bool pawnHidden, bool controllerHidden,
                                HiddenPlayer& out)
{
    out.Slot = slot;

    Controller controller = entities.Controller(slot);
    if (!controller)
    {
        return;
    }

    if (controllerHidden)
    {
        out.ControllerIndex = controller.Index();
    }

    if (!pawnHidden)
    {
        return;
    }

    Pawn pawn = controller.Pawn();
    if (!pawn)
    {
        return;
    }

    out.Pawn = pawn.Raw();
    AddIndex(out, pawn.Index());

    AddHandleVector(entities, out, pawn.WeaponServices().MyWeapons());
    AddHandleVector(entities, out, pawn.MyWearables());
}

Visibility::Visibility(EntitySystem& entities, const Bindings& bindings, SlotEvents& slots)
    : _entities(entities), _bindings(bindings)
{
    // SlotEvents fires on both fill and empty, so clearing on both edges handles recycled slots.
    _slotListener = slots.Changed += [this](int slot) {
        if (!IsValidSlot(slot))
        {
            return;
        }
        _state[slot] = {};
        // The owning effect normally cleans up first; this handles a vanished viewer.
        std::erase_if(_private, [slot](const PrivateEntity& e) { return e.Viewer == slot; });
    };
}

void Visibility::SetPawnHidden(int slot, bool hidden)
{
    if (IsValidSlot(slot))
    {
        _state[slot].PawnHidden = hidden;
    }
}

void Visibility::SetControllerHidden(int slot, bool hidden)
{
    if (IsValidSlot(slot))
    {
        _state[slot].ControllerHidden = hidden;
    }
}

bool Visibility::IsPawnHidden(int slot) const
{
    return IsValidSlot(slot) && _state[slot].PawnHidden;
}

bool Visibility::IsControllerHidden(int slot) const
{
    return IsValidSlot(slot) && _state[slot].ControllerHidden;
}

void Visibility::ShowOnlyTo(EntityRef entity, int slot)
{
    if (!entity || !IsValidSlot(slot))
    {
        return;
    }

    SetPrivate({.Entity = entity, .Viewer = slot});
}

void Visibility::HideFromTeam(EntityRef entity, Team team)
{
    if (!entity || !IsPlaying(team))
    {
        return;
    }

    SetPrivate({.Entity = entity, .HiddenFrom = team});
}

void Visibility::SetPrivate(const PrivateEntity& entry)
{
    const auto found = std::ranges::find(_private, entry.Entity, &PrivateEntity::Entity);
    if (found != _private.end())
    {
        *found = entry;
    }
    else
    {
        _private.push_back(entry);
    }
}

void Visibility::ShowToEveryone(EntityRef entity)
{
    std::erase_if(_private, [entity](const PrivateEntity& e) { return e.Entity == entity; });
}

std::shared_ptr<GlowVision> Visibility::CreateGlow(int viewerSlot, GlowConfig config)
{
    return std::make_shared<GlowVision>(_entities, *this, viewerSlot, std::move(config));
}

void Visibility::OnCheckTransmit(CCheckTransmitInfo** infoList, int infoCount)
{
    if (!_bindings.VisibilityRecipientSlot || !infoList)
    {
        return;
    }

    // Drop entries whose entity is gone because the engine recycles indices.
    for (auto& entry : _private)
    {
        const Entity entity = _entities.Get(entry.Entity);
        entry.Index = entity ? entity.Index() : -1;
    }
    std::erase_if(_private, [](const PrivateEntity& e) { return e.Index <= 0; });

    // Entity indices are shared by recipients; only self and observer exemptions vary per client.
    std::array<HiddenPlayer, MaxPlayers> hidden;
    int hiddenCount = 0;
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        const auto& state = _state[slot];
        if (state.Any())
        {
            CollectHiddenPlayer(_entities, slot, state.PawnHidden, state.ControllerHidden, hidden[hiddenCount++]);
        }
    }

    if (hiddenCount == 0 && _private.empty())
    {
        return;
    }

    for (int i = 0; i < infoCount; ++i)
    {
        auto* info = infoList[i];
        if (!info || !info->m_pTransmitEntity)
        {
            continue;
        }

        const int recipient = static_cast<int>(_bindings.VisibilityRecipientSlot.Read(info));
        CEntityInstance* observed = hiddenCount > 0 ? ObserverTarget(_entities, recipient) : nullptr;
        const Team recipientTeam = _entities.Controller(recipient).Team();

        for (int h = 0; h < hiddenCount; ++h)
        {
            const auto& player = hidden[h];
            if (player.Slot == recipient)
            {
                continue;
            }

            if (observed != player.Pawn)
            {
                for (int n = 0; n < player.IndexCount; ++n)
                {
                    info->m_pTransmitEntity->Clear(player.PawnIndices[n]);
                }
            }

            if (player.ControllerIndex > 0)
            {
                info->m_pTransmitEntity->Clear(player.ControllerIndex);
            }
        }

        for (const auto& entry : _private)
        {
            const bool hidden = entry.Viewer >= 0 ? entry.Viewer != recipient : entry.HiddenFrom == recipientTeam;
            if (hidden)
            {
                info->m_pTransmitEntity->Clear(entry.Index);
            }
        }
    }
}

}  // namespace VoltMod
