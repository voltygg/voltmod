#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Ui/UiLayout.hpp>
#include <utility>

namespace VoltMod
{

/** Reserved key for the input-capture flag, which is not a panel's dialog variable. */
static constexpr std::string_view kCaptureName = "input capture";

UiLayout::UiLayout(CustomUi& ui, SlotEvents& slots, std::string layout) : _ui(ui), _layout(std::move(layout))
{
    _cache.Bind(slots);

    // A slot changing hands is the one event that can make the entity too small, so it is also
    // the only thing that re-arms a re-spawn.
    _roster = slots.Changed += [this](int) { _rosterChanged = true; };
}

UiLayout::~UiLayout()
{
    // Handlers still holding a Subscription match on this ref; clearing it retires them before
    // the entity behind them goes away with _panel.
    *_live = {};
}

bool UiLayout::EnsureFor(int slot)
{
    if (!IsValidSlot(slot))
        return false;

    if (!_panel && !Spawn())
        return false;

    if (_panel.PlayerStateCount() > slot)
        return true;

    // The count is fixed when the entity spawns, so a roster that grew past it needs a new
    // entity. If a spawn made for this roster still does not cover the slot, this build has no
    // per-player layout state and retrying every frame would only churn entities.
    if (!_rosterChanged)
        return false;

    return Spawn() && _panel.PlayerStateCount() > slot;
}

void UiLayout::Text(int slot, std::string_view panelId, std::string_view variable, std::string_view value)
{
    if (_cache.Update(slot, panelId, variable, value))
        Wrote(slot, _panel.For(slot).SetText(panelId, variable, value), panelId);
}

void UiLayout::Class(int slot, std::string_view panelId, std::string_view className, bool on)
{
    if (_cache.Update(slot, panelId, className, on ? "1" : "0"))
        Wrote(slot, _panel.For(slot).SetClass(panelId, className, on), panelId);
}

void UiLayout::InputCapture(int slot, bool enabled)
{
    if (_cache.UpdateCapture(slot, enabled))
        Wrote(slot, _panel.For(slot).SetInputCapture(enabled), kCaptureName);
}

void UiLayout::Forget(int slot)
{
    _cache.Forget(slot);
}

Subscription UiLayout::OnClick(std::string buttonId, std::function<void(int slot)> handler)
{
    if (!handler)
        return {};

    // Matching on the shared ref rather than a copy is what makes this outlive a re-spawn, and
    // capturing it rather than `this` is what makes a stray Subscription harmless.
    return _ui.Clicks.Clicked +=
           [live = _live, id = std::move(buttonId), fn = std::move(handler)](const UiClick& click) {
               if (*live && click.Layout == *live && click.ButtonId == id)
                   fn(click.Slot);
           };
}

void UiLayout::Reset()
{
    _panel.Remove();
    *_live = {};
    _cache.ForgetAll();
}

bool UiLayout::Spawn()
{
    _rosterChanged = false;

    auto spawned = _ui.Spawn(_layout);
    if (!spawned)
    {
        Log::Warn("UiLayout '{}': spawn failed ({}).", _layout, spawned.error().Detail);
        return false;
    }

    // Assigning removes the entity this was driving, so nothing the old one was told survives.
    _panel = std::move(*spawned);
    *_live = _panel.Ref();
    _cache.ForgetAll();
    return true;
}

bool UiLayout::Wrote(int slot, const Status& status, std::string_view what)
{
    if (status)
        return true;

    // The cache has already recorded the value this write was meant to install, so it has to be
    // dropped or the next frame would skip the retry. Every write for the slot then fails the
    // same way, which is worth exactly one line rather than one per frame.
    _cache.Forget(slot);
    if (_cache.FirstFailure(slot))
        Log::Warn("UiLayout '{}': writing {} for slot {} failed ({}).", _layout, what, slot, status.error().Detail);

    return false;
}

}  // namespace VoltMod
