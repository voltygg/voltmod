#include "Hud/HudFields.hpp"
#include "Hud/LayoutName.hpp"

#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Hud/Hud.hpp>
#include <utility>

namespace VoltMod
{

Status HudPlayerView::SetText(std::string_view panelId, std::string_view variable, std::string_view value)
{
    return HudWriteText(_entities, _ref, _slot, panelId, variable, value);
}

Status HudPlayerView::SetClass(std::string_view panelId, std::string_view className, bool on)
{
    return HudWriteClass(_entities, _ref, _slot, panelId, className, on);
}

Status HudPlayerView::ResetClass(std::string_view panelId, std::string_view className)
{
    return HudResetClass(_entities, _ref, _slot, panelId, className);
}

Status HudPlayerView::SetInputCapture(bool enabled)
{
    return HudWriteInputCapture(_entities, _ref, _slot, enabled);
}

Result<bool> HudPlayerView::InputCaptureEnabled() const
{
    return HudReadInputCapture(_entities, _ref, _slot);
}

Hud::~Hud()
{
    Remove();
}

Hud::Hud(Hud&& other) noexcept
    : _entities(std::exchange(other._entities, nullptr)),
      _ops(std::exchange(other._ops, nullptr)),
      _clicked(std::exchange(other._clicked, nullptr)),
      _ref(std::exchange(other._ref, EntityRef{}))
{}

Hud& Hud::operator=(Hud&& other) noexcept
{
    if (this != &other)
    {
        Remove();
        _entities = std::exchange(other._entities, nullptr);
        _ops = std::exchange(other._ops, nullptr);
        _clicked = std::exchange(other._clicked, nullptr);
        _ref = std::exchange(other._ref, EntityRef{});
    }
    return *this;
}

Hud::operator bool() const
{
    return _entities && static_cast<bool>(_entities->Resolve(_ref));
}

void Hud::Remove()
{
    if (_entities && _ops)
    {
        if (Entity entity = _entities->Resolve(_ref))
            _ops->Remove(entity.Raw());
    }
    _ref = {};
}

Status Hud::SetText(std::string_view panelId, std::string_view variable, std::string_view value)
{
    return HudWriteText(_entities, _ref, kEveryone, panelId, variable, value);
}

Status Hud::SetClass(std::string_view panelId, std::string_view className, bool on)
{
    return HudWriteClass(_entities, _ref, kEveryone, panelId, className, on);
}

Status Hud::ResetClass(std::string_view panelId, std::string_view className)
{
    return HudResetClass(_entities, _ref, kEveryone, panelId, className);
}

Status Hud::SetInputCapture(bool enabled)
{
    return HudWriteInputCapture(_entities, _ref, kEveryone, enabled);
}

HudPlayerView Hud::For(int slot)
{
    return HudPlayerView(_entities, _ref, slot);
}

int Hud::PlayerStateCount() const
{
    return HudPlayerStateCount(_entities, _ref);
}

Subscription Hud::OnClick(std::string buttonId, std::function<void(int slot)> handler)
{
    if (!_clicked || !handler)
        return {};

    // The ref is captured by value, never `this`: the handler has to keep filtering correctly
    // after this handle moves, and must never reach back into one that has been destroyed.
    return *_clicked += [ref = _ref, id = std::move(buttonId), fn = std::move(handler)](const HudClick& click) {
        if (click.Layout == ref && click.ButtonId == id)
            fn(click.Slot);
    };
}

Subscription Hud::OnAnyClick(std::function<void(const HudClick&)> handler)
{
    if (!_clicked || !handler)
        return {};

    return *_clicked += [ref = _ref, fn = std::move(handler)](const HudClick& click) {
        if (click.Layout == ref)
            fn(click);
    };
}

Result<Hud> CustomHud::Spawn(std::string_view layout)
{
    auto resource = ResolveLayoutName(layout);
    if (!resource)
        return std::unexpected(resource.error());

    if (!_ops.CanSpawn())
        return std::unexpected(Error::Unsupported("entity spawning is unavailable"));

    KeyValues kv;
    kv.Set("layout", resource->c_str());

    CEntityInstance* entity = _ops.Spawn("custom_hud_layout", kv);
    if (!entity)
        return std::unexpected(Error::Engine("the engine refused to spawn custom_hud_layout"));

    return Hud(_entities, _ops, Clicks.Clicked, Entity(_entities, entity).Ref());
}

}  // namespace VoltMod
