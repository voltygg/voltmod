#include "Ui/LayoutName.hpp"
#include "Ui/PanelEntity.hpp"
#include "Ui/UiPanelState.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <format>
#include <utility>

namespace VoltMod
{

/** The key input capture is remembered under, since it is not a panel's dialog variable. */
static constexpr std::string_view CaptureKey = "input capture";

/** Where one write lands: the slot its value is remembered under, and the slot the engine is given. */
struct WriteTarget
{
    int CacheSlot;
    int EngineSlot;
};

/** A private panel has one audience: the viewer and @ref UiPanel::Everyone are the same request,
 *  cached under the viewer and written to the global state, which a client shows whatever pawn it
 *  is viewing. Input capture (@p perPlayer) stays on the viewer's own state, which the client reads
 *  for itself. Any other slot is refused. */
static Result<WriteTarget> ResolveTarget(const UiPanelState* state, int slot, bool perPlayer = false)
{
    if (!state || !state->IsPrivate())
        return WriteTarget{.CacheSlot = slot, .EngineSlot = slot};

    if (slot != UiPanel::Everyone && slot != state->Viewer)
        return std::unexpected(Error::Invalid(std::format("this panel is private to slot {}", state->Viewer)));

    return WriteTarget{.CacheSlot = state->Viewer, .EngineSlot = perPlayer ? state->Viewer : EveryoneSlot};
}

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

std::string_view UiPanel::LayoutName() const noexcept
{
    return _state ? std::string_view(_state->Layout) : std::string_view{};
}

EntityRef UiPanel::Entity() const noexcept
{
    return _state ? _state->CurrentEntity : EntityRef{};
}

int UiPanel::Viewer() const noexcept
{
    return _state ? _state->Viewer : Everyone;
}

int UiPanel::PlayerStateCount() const
{
    return _state ? PanelPlayerStateCount(_state->Entities, _state->CurrentEntity) : -1;
}

bool UiPanel::Prepare(int slot)
{
    if (!_state || (slot != Everyone && !IsValidSlot(slot)))
        return false;

    UiPanelState& state = *_state;
    // A private panel's input capture is per-player, so the entity has to cover the viewer.
    const auto target = ResolveTarget(&state, slot, /*perPlayer=*/true);
    if (!target)
        return false;
    slot = target->CacheSlot;

    if (!*this && !state.SpawnOrWarn())
        return false;

    if (slot == Everyone || state.CanWrite(slot))
        return true;

    // Per-player capacity is fixed at spawn; respawn only after the roster grows.
    if (!state.RosterChangedSinceSpawn)
        return false;

    return state.SpawnOrWarn() && state.CanWrite(slot);
}

bool UiPanel::CanWrite(int slot) const
{
    return _state && _state->CanWrite(slot);
}

Status UiPanel::SetText(int slot, std::string_view panelId, std::string_view variable, std::string_view value)
{
    UiPanelState* state = _state.get();
    const auto target = ResolveTarget(state, slot);
    if (!target)
        return std::unexpected(target.error());

    if (state && !state->Sent.Changed(target->CacheSlot, WriteKind::Variable, panelId, variable, value))
        return {};  // the viewer already has this value

    return WriteThrough(state, target->CacheSlot, panelId, [&](EntitySystem* entities, EntityRef panel) {
        return WritePanelText(entities, panel, target->EngineSlot, panelId, variable, value);
    });
}

Status UiPanel::SetClass(int slot, std::string_view panelId, std::string_view className, bool on)
{
    UiPanelState* state = _state.get();
    const auto target = ResolveTarget(state, slot);
    if (!target)
        return std::unexpected(target.error());

    if (state && !state->Sent.Changed(target->CacheSlot, WriteKind::Class, panelId, className, on ? "1" : "0"))
        return {};

    return WriteThrough(state, target->CacheSlot, panelId, [&](EntitySystem* entities, EntityRef panel) {
        return WritePanelClass(entities, panel, target->EngineSlot, panelId, className, on);
    });
}

Status UiPanel::RestoreClass(int slot, std::string_view panelId, std::string_view className)
{
    UiPanelState* state = _state.get();
    const auto target = ResolveTarget(state, slot);
    if (!target)
        return std::unexpected(target.error());

    // PanelEntity accepts a null system and dead ref for moved-from panels.
    const Status status = RestorePanelClass(state ? state->Entities : nullptr, state ? state->CurrentEntity : EntityRef{},
                                       target->EngineSlot, panelId, className);

    // The markup owns the current class state, so forget what was last sent.
    if (state && status)
        state->Sent.Forget(target->CacheSlot);

    return status;
}

Status UiPanel::SetInputCapture(int slot, bool enabled)
{
    UiPanelState* state = _state.get();
    const auto target = ResolveTarget(state, slot, /*perPlayer=*/true);
    if (!target)
        return std::unexpected(target.error());

    if (state && !state->Sent.CaptureChanged(target->CacheSlot, enabled))
        return {};

    return WriteThrough(state, target->CacheSlot, CaptureKey, [&](EntitySystem* entities, EntityRef panel) {
        return WritePanelInputCapture(entities, panel, target->EngineSlot, enabled);
    });
}

void UiPanel::ForgetWrites(int slot)
{
    if (_state)
        _state->Sent.Forget(slot);
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
    state.RosterChangedSinceSpawn = true;
}

Event<const UiClick&>& UiPanel::Clicked()
{
    return State().Clicked;
}

Event<int>& UiPanel::Pressed(std::string_view id)
{
    return State().Pressed(id);
}

UiPanelState& UiPanel::State()
{
    // Keep events usable on moved-from and default-constructed panels.
    if (!_state)
        _state = std::make_shared<UiPanelState>();

    return *_state;
}

}  // namespace VoltMod
