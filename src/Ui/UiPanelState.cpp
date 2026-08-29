#include "Ui/UiPanelState.hpp"

#include "Ui/UiFields.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <format>
#include <utility>

// What a panel keeps between calls: the entity, the write cache behind every per-slot write, and
// the one click subscription its events share. The handle in front of it is UiPanel.cpp.

namespace VoltMod
{

UiPanelState::UiPanelState(EntitySystem* entities, EntityOps* ops, SlotEvents* slots, Event<const UiClick&>* allClicks,
                           std::string layout, std::string resource)
    : Entities(entities),
      Ops(ops),
      AllClicks(allClicks),
      Layout(std::move(layout)),
      Resource(std::move(resource)),
      Clicked({.OnFirst = [this] { return OnFirstSubscriber(); }, .OnLast = [this] { OnLastSubscriber(); }})
{
    if (!slots)
        return;

    Cache.Bind(*slots);

    // A slot changing hands is the one thing that can make the entity too small, so it is also the
    // only thing that lets a re-spawn happen.
    PlayerChanges = slots->Changed += [this](int) { PlayersChanged = true; };
}

Status UiPanelState::Spawn()
{
    PlayersChanged = false;

    // Nothing the old entity was told survives, so it goes before the new one arrives.
    Remove();

    if (Resource.empty())
        return std::unexpected(Error::Invalid(std::format("'{}' is not a usable layout name", Layout)));
    if (!Ops || !Entities)
        return std::unexpected(Error::NotReady("this panel has no entity system behind it"));
    if (!Ops->CanSpawn())
        return std::unexpected(Error::Unsupported("entity spawning is unavailable"));

    KeyValues kv;
    kv.Set("layout", Resource);

    CEntityInstance* entity = Ops->Spawn("custom_hud_layout", kv);
    if (!entity)
        return std::unexpected(Error::Engine("the engine refused to spawn custom_hud_layout"));

    CurrentEntity = Entity(*Entities, entity).Ref();
    return {};
}

bool UiPanelState::SpawnOrWarn()
{
    const Status spawned = Spawn();
    if (!spawned)
        Log::Warn("UiPanel '{}': spawn failed ({}).", Layout, spawned.error().Detail);

    return spawned.has_value();
}

void UiPanelState::Remove()
{
    if (Entities && Ops)
    {
        if (Entity entity = Entities->Resolve(CurrentEntity))
            Ops->Remove(entity.Raw());
    }
    CurrentEntity = {};
    Cache.ForgetAll();
}

bool UiPanelState::Covers(int slot) const
{
    return IsValidSlot(slot) && UiPlayerStateCount(Entities, CurrentEntity) > slot;
}

Status UiPanelState::RecordWrite(int slot, Status status, std::string_view what)
{
    // A global write has no per-slot memory to drop and nothing to retry against, so it is the
    // caller's answer whichever way it went.
    if (status || !IsValidSlot(slot))
        return status;

    // The cache has already recorded the value this write was meant to install, so it has to be
    // dropped or the next frame would skip the retry. Every write for the slot then fails the same
    // way, which is worth exactly one line rather than one per frame.
    Cache.Forget(slot);
    if (Cache.FirstFailure(slot))
        Log::Warn("UiPanel '{}': writing {} for slot {} failed ({}).", Layout, what, slot, status.error().Detail);

    return status;
}

Event<int>& UiPanelState::Button(std::string_view id)
{
    if (auto it = Buttons.find(std::string(id)); it != Buttons.end())
        return it->second;

    return Buttons
        .try_emplace(std::string(id), Event<int>::Lifecycle{.OnFirst = [this] { return OnFirstSubscriber(); },
                                                            .OnLast = [this] { OnLastSubscriber(); }})
        .first->second;
}

bool UiPanelState::OnFirstSubscriber()
{
    if (Subscribers == 0)
    {
        if (!AllClicks)
            return false;

        // The handler holds this state, never the panel: the state is on the heap, so routing keeps
        // working after the panel moves, and ClickListener is declared last so the handler is
        // retired before anything it reads goes away.
        ClickListener = *AllClicks +=
            [this](const UiClick& click) { Internal::RouteUiClick(click, CurrentEntity, Clicked, Buttons); };
        if (!ClickListener)
            return false;  // the hook refused; a later subscription is free to try again
    }

    ++Subscribers;
    return true;
}

void UiPanelState::OnLastSubscriber()
{
    if (Subscribers > 0 && --Subscribers == 0)
        ClickListener.Reset();
}

}  // namespace VoltMod
