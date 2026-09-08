#include "Menu/PanoramaDriver.hpp"

#include "Menu/PanoramaIds.hpp"

#include <format>
#include <utility>

namespace VoltMod
{

PanoramaDriver::PanoramaDriver(ActiveMenus& menus, MenuSession& session, const MenuServices& services, UiPanel panel)
    : MenuDriver(menus, session, services), _panel(std::move(panel))
{
    _rows.reserve(RowsPerPageCount);
    for (int i = 0; i < RowsPerPageCount; ++i)
    {
        std::string row = std::format("vm_row{}", i);
        _rows.push_back({.Row = row, .Label = row + "_label", .Value = row + "_value"});
    }

    _pages.BindReset(services.Slots);

    // Reset screen state as well as the manager's per-slot menu state.
    _subs.Add(services.Slots.Changed += [this](int slot) { Dismiss(slot); });
}

PanoramaDriver::~PanoramaDriver() = default;

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

    _clicks = _panel.Clicked() += [this](const UiClick& click) { OnClick(click); };
}

void PanoramaDriver::OnClick(const UiClick& click)
{
    const int slot = click.Slot;
    if (!IsValidSlot(slot) || !_menus.Current(slot))
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
    // Written straight from here rather than deferred to the next frame: presses are delivered on
    // the game frame, so a close that came from a row handler is already outside the engine's
    // inbound message path and may write to the entity.
    if (!_panel.Covers(slot))
        return;

    (void)_panel.Class(slot, RootId, Css::Hidden, true);
    (void)_panel.InputCapture(slot, false);

    _panel.Forget(slot);
}

}  // namespace VoltMod
