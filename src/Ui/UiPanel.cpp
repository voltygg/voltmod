#include "Ui/LayoutName.hpp"
#include "Ui/UiFields.hpp"
#include "Ui/UiPanelState.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <utility>

namespace VoltMod
{

// UiPanel::Everyone and kEveryone address the same global state.
static_assert(UiPanel::Everyone == kEveryone, "UiPanel::Everyone must be the UiFields global-state slot.");

/** Reserved cache key for the input-capture flag, which is not a panel's dialog variable. */
static constexpr std::string_view kCaptureName = "input capture";

/**
 * Empty panels use the setter's null path. Writes that reach an entity are cached so failed
 * per-slot writes can retry and log once. The template avoids per-frame allocations.
 */
template <class Write>
static Status WriteThrough(UiPanelState* state, int slot, std::string_view what, Write write)
{
    if (!state)
        return write(nullptr, EntityRef{});

    return state->RecordWrite(slot, write(state->Entities, state->CurrentEntity), what);
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
    if (!*this && !state.SpawnOrWarn())
        return false;

    if (slot == Everyone || state.Covers(slot))
        return true;

    // Per-player capacity is fixed at spawn; respawn only after the roster grows.
    if (!state.PlayersChanged)
        return false;

    return state.SpawnOrWarn() && state.Covers(slot);
}

bool UiPanel::Covers(int slot) const
{
    return _state && _state->Covers(slot);
}

Status UiPanel::Text(int slot, std::string_view panelId, std::string_view variable, std::string_view value)
{
    UiPanelState* state = _state.get();
    if (state && slot != Everyone && !state->Cache.Update(slot, UiProperty::Text, panelId, variable, value))
        return {};  // the player already has this value

    return WriteThrough(state, slot, panelId, [&](EntitySystem* entities, EntityRef panel) {
        return UiWriteText(entities, panel, slot, panelId, variable, value);
    });
}

Status UiPanel::Class(int slot, std::string_view panelId, std::string_view className, bool on)
{
    UiPanelState* state = _state.get();
    if (state && slot != Everyone && !state->Cache.Update(slot, UiProperty::Class, panelId, className, on ? "1" : "0"))
        return {};

    return WriteThrough(state, slot, panelId, [&](EntitySystem* entities, EntityRef panel) {
        return UiWriteClass(entities, panel, slot, panelId, className, on);
    });
}

Status UiPanel::ResetClass(int slot, std::string_view panelId, std::string_view className)
{
    UiPanelState* state = _state.get();
    // UiFields accepts a null system and dead ref for moved-from panels.
    const Status status = UiResetClass(state ? state->Entities : nullptr, state ? state->CurrentEntity : EntityRef{},
                                       slot, panelId, className);

    // The markup owns the current class state, so discard cached per-slot values.
    if (state && status && slot != Everyone)
        state->Cache.Forget(slot);

    return status;
}

Status UiPanel::InputCapture(int slot, bool enabled)
{
    UiPanelState* state = _state.get();
    if (state && slot != Everyone && !state->Cache.UpdateCapture(slot, enabled))
        return {};

    return WriteThrough(state, slot, kCaptureName, [&](EntitySystem* entities, EntityRef panel) {
        return UiWriteInputCapture(entities, panel, slot, enabled);
    });
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

    // Request a new spawn on the next write instead of waiting for a roster change.
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
    // Keep events usable on moved-from and default-constructed panels.
    if (!_state)
        _state = std::make_shared<UiPanelState>();

    return *_state;
}

}  // namespace VoltMod
