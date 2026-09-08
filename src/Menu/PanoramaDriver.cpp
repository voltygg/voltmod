#include "Menu/PanoramaDriver.hpp"

#include "Menu/PanoramaIds.hpp"

#include <VoltMod/Core/Log.hpp>
#include <format>
#include <utility>

namespace VoltMod
{

PanoramaDriver::PanoramaDriver(ActiveMenus& menus, MenuSession& session, const MenuServices& services,
                               std::string layout)
    : MenuDriver(menus, session, services), _layout(std::move(layout))
{
    _rows.reserve(RowsPerPageCount);
    for (int i = 0; i < RowsPerPageCount; ++i)
    {
        std::string row = std::format("vm_row{}", i);
        _rows.push_back({.Row = row, .Label = row + "_label", .Value = row + "_value"});
    }

    _pages.BindReset(services.Slots);
    _panels.BindReset(services.Slots);
}

PanoramaDriver::~PanoramaDriver() = default;

UiPanel& PanoramaDriver::PanelFor(int slot)
{
    UiPanel& panel = _panels[slot];
    if (panel.Viewer() == slot)
        return panel;

    auto made = _services.Ui.Panel(_layout, slot);
    if (made)
        panel = std::move(*made);
    else
        Log::Warn("Menu: no private panel for slot {} ({}).", slot, made.error().Detail);

    return panel;
}

bool PanoramaDriver::HandleInput(int slot)
{
    return _menus.KeyboardEnabled(slot) && HandleKeys(slot);
}

void PanoramaDriver::Reset(int slot)
{
    // Preserve the cursor's page, including after a center-HTML fallback.
    const int selected = _menus.Selected(slot);
    _pages[slot] = selected > 0 ? selected / RowsPerPageCount : 0;
}

void PanoramaDriver::ShowPage(int slot, int page)
{
    _pages[slot] = page < 0 ? 0 : page;
}

int PanoramaDriver::ItemIndex(int slot, int row) const
{
    return _pages[slot] * RowsPerPageCount + row;
}

void PanoramaDriver::BindClicks()
{
    if (_clicks)
        return;

    _clicks = _services.Ui.Clicked += [this](const UiClick& click) { OnClick(click); };
}

void PanoramaDriver::OnClick(const UiClick& click)
{
    const int slot = click.Slot;
    if (!IsValidSlot(slot) || !_menus.Current(slot))
        return;

    // Only presses on this player's own panel.
    const EntityRef own = _panels[slot].Ref();
    if (!own || click.Layout != own)
        return;

    const MenuPress press = ParseMenuButton(click.ButtonId);

    // Captures honor Cancel; Back and Close remain available to leave the menu.
    switch (press.Button)
    {
    case MenuButton::None:
        return;
    case MenuButton::Cancel:
        _services.ChatInput.CancelCapture(slot);
        return;
    case MenuButton::Back:
        _session.Close(slot);
        return;
    case MenuButton::Close:
        _session.CloseAll(slot);
        return;
    default:
        break;
    }

    if (_services.ChatInput.IsCapturing(slot))
        return;

    if (press.Button == MenuButton::Prev || press.Button == MenuButton::Next)
    {
        TurnPage(slot, press.Button == MenuButton::Prev ? -1 : +1);
        return;
    }

    // Remaining presses are row presses and carry a row index.
    if (press.Row < 0 || press.Row >= RowsPerPageCount)
        return;

    // Keep keyboard navigation at the clicked row.
    const int index = ItemIndex(slot, press.Row);
    _menus.Select(slot, index);

    switch (press.Button)
    {
    case MenuButton::Row:
        _menus.Activate(slot, index);
        break;
    case MenuButton::RowDec:
        (void)_menus.Step(slot, index, -1);
        break;
    case MenuButton::RowInc:
        (void)_menus.Step(slot, index, +1);
        break;
    default:
        break;
    }
}

void PanoramaDriver::TurnPage(int slot, int delta)
{
    auto* menu = _menus.Current(slot);
    if (!menu)
        return;

    const int pages = PageCount(static_cast<int>(menu->Items.size()), RowsPerPageCount);
    _pages[slot] = WrapIndex(_pages[slot] + delta, pages);
    // Keep the cursor on the page being drawn.
    _menus.SelectOnPage(slot, _pages[slot], RowsPerPageCount);
}

void PanoramaDriver::Dismiss(int slot)
{
    // Safe to write from here: presses arrive on the game frame, outside the engine's inbound
    // message path. The capture goes before the entity does, so nobody is left holding one.
    UiPanel& panel = _panels[slot];
    if (panel.Covers(slot))
    {
        (void)panel.Class(slot, RootId, Css::Hidden, true);
        (void)panel.InputCapture(slot, false);
    }

    // A closed menu costs nothing: this removes the entity and its Transmit entry, and the next
    // Present makes a fresh panel.
    _panels[slot] = {};
}

}  // namespace VoltMod
