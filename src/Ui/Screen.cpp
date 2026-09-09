#include <VoltMod/Ui/Screen.hpp>

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>

namespace VoltMod
{

Screen::Screen(CustomUi& ui, SlotEvents& slots, std::string_view layout, std::string_view rootId, int viewer)
    : _root(rootId)
{
    _shown.BindReset(slots);

    Result<UiPanel> panel = ui.Panel(layout, viewer);
    if (!panel)
    {
        Log::Warn("Screen '{}': {}", layout, panel.error().Detail);
        return;
    }
    _panel = std::move(*panel);
}

bool Screen::Show(int slot, bool capture)
{
    if (!_panel.Ensure(slot))
        return false;

    _panel.Class(slot, _root, "Hidden", false);
    if (capture)
        _panel.InputCapture(slot, true);

    if (IsValidSlot(slot))
        _shown[slot] = true;
    return true;
}

void Screen::Hide(int slot)
{
    _panel.Class(slot, _root, "Hidden", true);
    if (IsValidSlot(slot))
        _shown[slot] = false;
}

bool Screen::Shown(int slot) const
{
    return IsValidSlot(slot) && _shown[slot];
}

UiPanel& Screen::Panel()
{
    return _panel;
}

Event<int>& Screen::Button(std::string_view id)
{
    return _panel.Button(id);
}

std::string_view Screen::Layout() const noexcept
{
    return _panel.Name();
}

}  // namespace VoltMod
