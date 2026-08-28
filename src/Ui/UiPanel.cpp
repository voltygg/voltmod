#include "Ui/LayoutName.hpp"
#include "Ui/UiFields.hpp"
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

// The public spelling of what UiFields calls kEveryone; they index the same engine setters.
static_assert(UiPanel::Everyone == kEveryone, "UiPanel::Everyone must be the UiFields global-state slot.");

/** Reserved cache key for the input-capture flag, which is not a panel's dialog variable. */
static constexpr std::string_view kCaptureName = "input capture";

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
    if (status)
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

/** Spawn and say why it failed, once per attempt rather than once per frame. */
static bool SpawnOrWarn(UiPanelState& state)
{
    const Status spawned = state.Spawn();
    if (!spawned)
        Log::Warn("UiPanel '{}': spawn failed ({}).", state.Layout, spawned.error().Detail);

    return spawned.has_value();
}

UiPanel::~UiPanel()
{
    Remove();
}

UiPanel& UiPanel::operator=(UiPanel&& other) noexcept
{
    if (this != &other)
    {
        Remove();
        _state = std::move(other._state);
    }
    return *this;
}

UiPanel::operator bool() const
{
    return _state && _state->Entities && static_cast<bool>(_state->Entities->Resolve(_state->CurrentEntity));
}

std::string_view UiPanel::Name() const noexcept
{
    return _state ? std::string_view(_state->Layout) : std::string_view{};
}

EntityRef UiPanel::Ref() const noexcept
{
    return _state ? _state->CurrentEntity : EntityRef{};
}

int UiPanel::PlayerStateCount() const
{
    return _state ? UiPlayerStateCount(_state->Entities, _state->CurrentEntity) : -1;
}

bool UiPanel::Ensure(int slot)
{
    if (!_state || (slot != Everyone && !IsValidSlot(slot)))
        return false;

    UiPanelState& state = *_state;
    if (!*this && !SpawnOrWarn(state))
        return false;

    // A global write needs the entity and nothing else.
    if (slot == Everyone || state.Covers(slot))
        return true;

    // The per-player state count is fixed when the entity spawns, so a server that has since
    // grown past it needs a new entity. If a spawn made for these players still does not cover the
    // slot, this build has no per-player layout state and retrying every frame would only churn
    // entities.
    if (!state.PlayersChanged)
        return false;

    return SpawnOrWarn(state) && state.Covers(slot);
}

bool UiPanel::Covers(int slot) const
{
    return _state && _state->Covers(slot);
}

Status UiPanel::Text(int slot, std::string_view panelId, std::string_view variable, std::string_view value)
{
    if (!_state)
        return UiWriteText(nullptr, {}, slot, panelId, variable, value);

    UiPanelState& state = *_state;
    if (slot == Everyone)
        return UiWriteText(state.Entities, state.CurrentEntity, Everyone, panelId, variable, value);

    if (!state.Cache.Update(slot, UiProperty::Text, panelId, variable, value))
        return {};

    return state.RecordWrite(slot, UiWriteText(state.Entities, state.CurrentEntity, slot, panelId, variable, value),
                             panelId);
}

Status UiPanel::Class(int slot, std::string_view panelId, std::string_view className, bool on)
{
    if (!_state)
        return UiWriteClass(nullptr, {}, slot, panelId, className, on);

    UiPanelState& state = *_state;
    if (slot == Everyone)
        return UiWriteClass(state.Entities, state.CurrentEntity, Everyone, panelId, className, on);

    if (!state.Cache.Update(slot, UiProperty::Class, panelId, className, on ? "1" : "0"))
        return {};

    return state.RecordWrite(slot, UiWriteClass(state.Entities, state.CurrentEntity, slot, panelId, className, on),
                             panelId);
}

Status UiPanel::ResetClass(int slot, std::string_view panelId, std::string_view className)
{
    if (!_state)
        return UiResetClass(nullptr, {}, slot, panelId, className);

    UiPanelState& state = *_state;
    const Status status = UiResetClass(state.Entities, state.CurrentEntity, slot, panelId, className);

    // The markup, not the server, decides what the class is now, so everything the cache believes
    // about this slot is a guess. Dropping it costs one redundant redraw and keeps the rest honest.
    if (status && slot != Everyone)
        state.Cache.Forget(slot);

    return status;
}

Status UiPanel::InputCapture(int slot, bool enabled)
{
    if (!_state)
        return UiWriteInputCapture(nullptr, {}, slot, enabled);

    UiPanelState& state = *_state;
    if (slot == Everyone)
        return UiWriteInputCapture(state.Entities, state.CurrentEntity, Everyone, enabled);

    if (!state.Cache.UpdateCapture(slot, enabled))
        return {};

    return state.RecordWrite(slot, UiWriteInputCapture(state.Entities, state.CurrentEntity, slot, enabled),
                             kCaptureName);
}

Result<bool> UiPanel::InputCaptured(int slot) const
{
    if (!_state)
        return UiReadInputCapture(nullptr, {}, slot);

    return UiReadInputCapture(_state->Entities, _state->CurrentEntity, slot);
}

void UiPanel::Forget(int slot)
{
    if (_state)
        _state->Cache.Forget(slot);
}

void UiPanel::Remove()
{
    if (_state)
        _state->Remove();
}

void UiPanel::SetLayout(std::string layout)
{
    if (!_state || layout == _state->Layout)
        return;

    UiPanelState& state = *_state;
    state.Remove();

    auto resource = ResolveLayoutName(layout);
    if (!resource)
        Log::Warn("UiPanel: '{}' is not a usable layout name ({}).", layout, resource.error().Detail);

    state.Layout = std::move(layout);
    state.Resource = resource ? std::move(*resource) : std::string{};

    // The old entity is gone and the new one has not been asked for; without this a spawn only
    // happens the next time a player connects or disconnects.
    state.PlayersChanged = true;
}

Event<const UiClick&>& UiPanel::Clicked()
{
    return State().Clicked;
}

Event<int>& UiPanel::Button(std::string_view id)
{
    return State().Button(id);
}

UiPanelState& UiPanel::State()
{
    // A moved-from or default-constructed panel still has to hand out a live event to subscribe
    // to; one with no engine behind it simply never raises.
    if (!_state)
        _state = std::make_shared<UiPanelState>();

    return *_state;
}

}  // namespace VoltMod
