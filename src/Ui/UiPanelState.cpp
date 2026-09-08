#include "Ui/UiPanelState.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <format>
#include <utility>

namespace VoltMod
{

UiPanelState::UiPanelState(EntitySystem* entities, EntityOps* ops, SlotEvents* slots, Event<const UiClick&>* allClicks,
                           std::string layout, std::string resource, Visibility* visibility, int viewer)
    : Entities(entities),
      Ops(ops),
      AllClicks(allClicks),
      Layout(std::move(layout)),
      Resource(std::move(resource)),
      Exclusive(visibility),
      Viewer(viewer),
      ClickRouting(
          "UiPanel", [this] { return StartClickRouting(); }, [this] { StopClickRouting(); }),
      Clicked(ClickRouting.ForEvent())
{
    if (!slots)
        return;

    Cache.Bind(*slots);

    PlayerChanges = slots->Changed += [this](int slot) {
        PlayersChanged = true;  // the entity's per-player capacity is fixed at spawn
        if (slot == Viewer)     // a private panel goes with its viewer
            Remove();
    };
}

Status UiPanelState::Spawn()
{
    PlayersChanged = false;

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
    if (IsPrivate() && Exclusive)
        Exclusive->ShowOnlyTo(CurrentEntity, Viewer);
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
    if (Exclusive)
        Exclusive->ShowToEveryone(CurrentEntity);
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
    if (IsPrivate() && slot != Viewer)
        return false;
    return IsValidSlot(slot) && UiPlayerStateCount(Entities, CurrentEntity) > slot;
}

Status UiPanelState::RecordWrite(int slot, Status status, std::string_view what)
{
    if (status || !IsValidSlot(slot))
        return status;

    // Drop the failed value so the next frame retries it, but log only once per slot.
    Cache.Forget(slot);
    if (Cache.FirstFailure(slot))
        Log::Warn("UiPanel '{}': writing {} for slot {} failed ({}).", Layout, what, slot, status.error().Detail);

    return status;
}

Event<int>& UiPanelState::Button(std::string_view id)
{
    if (auto it = Buttons.find(std::string(id)); it != Buttons.end())
        return it->second;

    return Buttons.try_emplace(std::string(id), ClickRouting.ForEvent()).first->second;
}

bool UiPanelState::StartClickRouting()
{
    if (!AllClicks)
        return false;

    // Capture heap-owned state so routing survives panel moves. ClickListener is destroyed
    // before the state it reads.
    ClickListener = *AllClicks +=
        [this](const UiClick& click) { Internal::RouteUiClick(click, CurrentEntity, Clicked, Buttons); };

    // Empty means the hook refused; a later subscription is free to try again.
    return static_cast<bool>(ClickListener);
}

void UiPanelState::StopClickRouting()
{
    ClickListener.Reset();
}

}  // namespace VoltMod
