#include "Ui/LayoutName.hpp"
#include "Ui/UiFields.hpp"

#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <utility>

namespace VoltMod
{

Status UiPlayerView::SetText(std::string_view panelId, std::string_view variable, std::string_view value)
{
    return UiWriteText(_entities, _ref, _slot, panelId, variable, value);
}

Status UiPlayerView::SetClass(std::string_view panelId, std::string_view className, bool on)
{
    return UiWriteClass(_entities, _ref, _slot, panelId, className, on);
}

Status UiPlayerView::ResetClass(std::string_view panelId, std::string_view className)
{
    return UiResetClass(_entities, _ref, _slot, panelId, className);
}

Status UiPlayerView::SetInputCapture(bool enabled)
{
    return UiWriteInputCapture(_entities, _ref, _slot, enabled);
}

Result<bool> UiPlayerView::InputCaptureEnabled() const
{
    return UiReadInputCapture(_entities, _ref, _slot);
}

UiPanel::~UiPanel()
{
    Remove();
}

UiPanel::UiPanel(UiPanel&& other) noexcept
    : _entities(std::exchange(other._entities, nullptr)),
      _ops(std::exchange(other._ops, nullptr)),
      _clicked(std::exchange(other._clicked, nullptr)),
      _ref(std::exchange(other._ref, EntityRef{}))
{}

UiPanel& UiPanel::operator=(UiPanel&& other) noexcept
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

UiPanel::operator bool() const
{
    return _entities && static_cast<bool>(_entities->Resolve(_ref));
}

void UiPanel::Remove()
{
    if (_entities && _ops)
    {
        if (Entity entity = _entities->Resolve(_ref))
            _ops->Remove(entity.Raw());
    }
    _ref = {};
}

Status UiPanel::SetText(std::string_view panelId, std::string_view variable, std::string_view value)
{
    return UiWriteText(_entities, _ref, kEveryone, panelId, variable, value);
}

Status UiPanel::SetClass(std::string_view panelId, std::string_view className, bool on)
{
    return UiWriteClass(_entities, _ref, kEveryone, panelId, className, on);
}

Status UiPanel::ResetClass(std::string_view panelId, std::string_view className)
{
    return UiResetClass(_entities, _ref, kEveryone, panelId, className);
}

Status UiPanel::SetInputCapture(bool enabled)
{
    return UiWriteInputCapture(_entities, _ref, kEveryone, enabled);
}

UiPlayerView UiPanel::For(int slot)
{
    return UiPlayerView(_entities, _ref, slot);
}

int UiPanel::PlayerStateCount() const
{
    return UiPlayerStateCount(_entities, _ref);
}

Subscription UiPanel::OnClick(std::string buttonId, std::function<void(int slot)> handler)
{
    if (!_clicked || !handler)
        return {};

    // The ref is captured by value, never `this`: the handler has to keep filtering correctly
    // after this handle moves, and must never reach back into one that has been destroyed.
    return *_clicked += [ref = _ref, id = std::move(buttonId), fn = std::move(handler)](const UiClick& click) {
        if (click.Layout == ref && click.ButtonId == id)
            fn(click.Slot);
    };
}

Subscription UiPanel::OnAnyClick(std::function<void(const UiClick&)> handler)
{
    if (!_clicked || !handler)
        return {};

    return *_clicked += [ref = _ref, fn = std::move(handler)](const UiClick& click) {
        if (click.Layout == ref)
            fn(click);
    };
}

Result<UiPanel> CustomUi::Spawn(std::string_view layout)
{
    auto resource = ResolveLayoutName(layout);
    if (!resource)
        return std::unexpected(resource.error());

    if (!_ops.CanSpawn())
        return std::unexpected(Error::Unsupported("entity spawning is unavailable"));

    KeyValues kv;
    kv.Set("layout", *resource);

    CEntityInstance* entity = _ops.Spawn("custom_hud_layout", kv);
    if (!entity)
        return std::unexpected(Error::Engine("the engine refused to spawn custom_hud_layout"));

    return UiPanel(_entities, _ops, Clicks.Clicked, Entity(_entities, entity).Ref());
}

}  // namespace VoltMod
