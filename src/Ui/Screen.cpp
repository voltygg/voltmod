#include <VoltMod/Ui/Screen.hpp>

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>

namespace VoltMod
{

Screen::Screen(UiPanels& ui, std::string_view layout, std::string_view rootId)
    : _ui(ui), _layout(layout), _root(rootId), _perPlayer(false)
{
    if (auto panel = ui.Panel(_layout, UiPanel::Everyone))
        _shared = std::move(*panel);
    else
        Log::Warn("Screen '{}': {}", _layout, panel.error().Detail);
}

Screen::Screen(UiPanels& ui, SlotEvents& slots, std::string_view layout, std::string_view rootId)
    : _ui(ui), _layout(layout), _root(rootId), _perPlayer(true)
{
    _private.BindReset(slots);
}

UiPanel& Screen::Panel(int slot)
{
    if (!_perPlayer || !IsValidSlot(slot))
        return _shared;

    UiPanel& panel = _private[slot];
    if (!panel)
    {
        if (auto made = _ui.Panel(_layout, slot))
            panel = std::move(*made);
        else
            Log::Warn("Screen '{}' for slot {}: {}", _layout, slot, made.error().Detail);
    }
    return panel;
}

bool Screen::Show(int slot, bool capture)
{
    // A private screen has no panel to spawn for the global viewer.
    if (_perPlayer && !IsValidSlot(slot))
        return false;

    UiPanel& panel = Panel(slot);
    if (!panel.Prepare(slot))
        return false;

    panel.SetClass(slot, _root, "Hidden", false);
    if (capture)
        panel.SetInputCapture(slot, true);
    return true;
}

void Screen::Hide(int slot)
{
    UiPanel& panel = Panel(slot);
    if (!panel)
        return;

    // Capture is per-player whatever the panel is, so only a real slot ever holds one.
    if (IsValidSlot(slot))
        panel.SetInputCapture(slot, false);
    panel.SetClass(slot, _root, "Hidden", true);
}

std::string_view Screen::Layout() const noexcept
{
    return _layout;
}

}  // namespace VoltMod
