#include "Ui/LayoutName.hpp"
#include "Ui/UiFields.hpp"
#include "Ui/UiPanelState.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <utility>

// The handle: ownership, the spawn gate every write sits behind, and the writes themselves. What
// a panel remembers between calls is UiPanelState.cpp.

namespace VoltMod
{

// The public spelling of what UiFields calls kEveryone; they index the same engine setters.
static_assert(UiPanel::Everyone == kEveryone, "UiPanel::Everyone must be the UiFields global-state slot.");

/** Reserved cache key for the input-capture flag, which is not a panel's dialog variable. */
static constexpr std::string_view kCaptureName = "input capture";

/**
 * The shape every write shares: an empty panel answers through the setter's own null path, and a
 * write that reached an entity is recorded, so a per-slot failure drops what the cache just took
 * down and says why once. @p write is handed the entity to address; @p what names the write for
 * that one line. A template rather than a `std::function` so a frame of writes allocates nothing.
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

    // A global write needs the entity and nothing else.
    if (slot == Everyone || state.Covers(slot))
        return true;

    // The per-player state count is fixed when the entity spawns, so a server that has since
    // grown past it needs a new entity. If a spawn made for these players still does not cover the
    // slot, this build has no per-player layout state and retrying every frame would only churn
    // entities.
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
    // The UiFields calls take a null system and a dead ref by design, so the moved-from panel
    // takes the same call rather than a second spelling of it.
    const Status status = UiResetClass(state ? state->Entities : nullptr, state ? state->CurrentEntity : EntityRef{},
                                       slot, panelId, className);

    // Not recorded like the writes above: the markup, not the server, decides what the class is
    // now, so everything the cache believes about this slot is a guess. Dropping it costs one
    // redundant redraw and keeps the rest honest.
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
