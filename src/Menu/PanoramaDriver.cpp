#include "Menu/PanoramaDriver.hpp"

#include "Menu/PanoramaIds.hpp"

#include <format>
#include <utility>

// The driver's lifecycle and input: the row ids it writes through, the presses coming back from
// the layout, and the page each player is on. The writes themselves are in PanoramaDraw.cpp.

namespace VoltMod
{

PanoramaDriver::PanoramaDriver(MenuManager& menus, const MenuServices& services, UiPanel panel)
    : MenuDriver(menus, services), _panel(std::move(panel))
{
    _rows.reserve(RowsPerPageCount);
    for (int i = 0; i < RowsPerPageCount; ++i)
    {
        std::string row = std::format("vm_row{}", i);
        _rows.push_back({.Row = row, .Label = row + "_label", .Value = row + "_value"});
    }

    _pages.BindReset(services.Slots);

    // The entity's per-player state belongs to the slot, not the player, so a player who left with
    // a menu open would hand it to the next occupant. The stack itself is cleared by the manager's
    // own PerSlot; this is the screen half of the same reset.
    _subs.On(services.Slots.Changed, [this](int slot) { Dismiss(slot); });
}

PanoramaDriver::~PanoramaDriver() = default;

bool PanoramaDriver::HandleInput(int slot)
{
    return KeyboardEnabled(slot) && HandleKeys(slot);
}

void PanoramaDriver::Reset(int slot)
{
    _pages[slot] = 0;
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
    if (!IsValidSlot(slot) || !Current(slot))
        return;

    const MenuPress press = ParseMenuButton(click.ButtonId);

    // Cancel is the one press a chat prompt honours; Back and Close stay live because a player
    // who wants out of the menu should not have to answer the prompt first.
    switch (press.Button)
    {
    case MenuButton::None:
        return;
    case MenuButton::Cancel:
        _services.ChatInput.CancelCapture(slot);
        return;
    case MenuButton::Back:
        _menus.Close(slot);
        return;
    case MenuButton::Close:
        _menus.CloseAll(slot);
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

    // Everything left is a row press, which is the only kind that carries a row index.
    if (press.Row < 0 || press.Row >= RowsPerPageCount)
        return;

    // A click moves the cursor there too, so the keyboard carries on from what was pressed rather
    // than from wherever it was left.
    const int index = ItemIndex(slot, press.Row);
    Select(slot, index);

    switch (press.Button)
    {
    case MenuButton::Row:
        Activate(slot, index);
        break;
    case MenuButton::RowDec:
        (void)Step(slot, index, -1);
        break;
    case MenuButton::RowInc:
        (void)Step(slot, index, +1);
        break;
    default:
        break;
    }
}

void PanoramaDriver::TurnPage(int slot, int delta)
{
    auto* menu = Current(slot);
    if (!menu)
        return;

    const int pages = PageCount(static_cast<int>(menu->Items.size()), RowsPerPageCount);
    _pages[slot] = WrapIndex(_pages[slot] + delta, pages);
    // The cursor follows the page rather than sitting on a row that is no longer drawn.
    SelectOnPage(slot, _pages[slot]);
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
