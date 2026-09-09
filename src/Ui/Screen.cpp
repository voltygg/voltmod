#include <VoltMod/Ui/Screen.hpp>

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>

namespace VoltMod
{

Screen::Screen(CustomUi& ui, std::string_view layout, std::string_view rootId)
    : _ui(ui), _layout(layout), _root(rootId), _perViewer(false)
{
    if (auto panel = ui.Panel(_layout, UiPanel::Everyone))
        _shared = std::move(*panel);
    else
        Log::Warn("Screen '{}': {}", _layout, panel.error().Detail);
}

Screen::Screen(CustomUi& ui, SlotEvents& slots, std::string_view layout, std::string_view rootId)
    : _ui(ui), _layout(layout), _root(rootId), _perViewer(true)
{
    _private.BindReset(slots);
}

UiPanel& Screen::Panel(int slot)
{
    if (!_perViewer || !IsValidSlot(slot))
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
    if (_perViewer && !IsValidSlot(slot))
        return false;

    UiPanel& panel = Panel(slot);
    if (!panel.Ensure(slot))
        return false;

    panel.Class(slot, _root, "Hidden", false);
    if (capture)
        panel.InputCapture(slot, true);
    return true;
}

void Screen::Hide(int slot)
{
    UiPanel& panel = Panel(slot);
    if (!panel)
        return;

    // Capture is per-player whatever the panel is, so only a real slot ever holds one.
    if (IsValidSlot(slot))
        panel.InputCapture(slot, false);
    panel.Class(slot, _root, "Hidden", true);
}

std::string_view Screen::Layout() const noexcept
{
    return _layout;
}

}  // namespace VoltMod
