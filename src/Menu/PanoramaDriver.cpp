#include "Menu/PanoramaDriver.hpp"

#include "Menu/PanoramaIds.hpp"

#include <VoltMod/Core/EnumNames.hpp>
#include <algorithm>
#include <format>
#include <utility>

namespace VoltMod
{

static constexpr std::string_view kHidden = "Hidden";
static constexpr std::string_view kDisabled = "Disabled";
static constexpr std::string_view kHasValue = "HasValue";
static constexpr std::string_view kHasSteppers = "HasSteppers";

PanoramaDriver::PanoramaDriver(MenuManager& menus, const MenuServices& services, UiPanel panel)
    : MenuDriver(menus, services), _panel(std::move(panel))
{
    _rows.reserve(RowsPerPage);
    for (int i = 0; i < RowsPerPage; ++i)
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

std::string_view PanoramaDriver::ClassFor(MenuRowKind kind)
{
    switch (kind)
    {
    case MenuRowKind::Text:
        return "Kind--text";
    case MenuRowKind::Submenu:
        return "Kind--submenu";
    case MenuRowKind::Toggle:
        return "Kind--toggle";
    case MenuRowKind::Choice:
        return "Kind--choice";
    case MenuRowKind::Input:
        return "Kind--input";
    case MenuRowKind::Button:
        break;
    }
    return "Kind--button";
}

bool PanoramaDriver::HandleInput(int)
{
    return false;
}

void PanoramaDriver::Reset(int slot)
{
    _pages[slot] = 0;
}

int PanoramaDriver::ItemIndex(int slot, int row) const
{
    return _pages[slot] * RowsPerPage + row;
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
    if (press.Button == MenuButton::None)
        return;

    // Cancel is the one press a chat prompt honours; Back and Close stay live because a player
    // who wants out of the menu should not have to answer the prompt first.
    if (press.Button == MenuButton::Cancel)
    {
        _services.ChatInput.CancelCapture(slot);
        return;
    }
    if (press.Button == MenuButton::Back)
    {
        _menus.Close(slot);
        return;
    }
    if (press.Button == MenuButton::Close)
    {
        _menus.CloseAll(slot);
        return;
    }

    if (_services.ChatInput.IsCapturing(slot))
        return;

    switch (press.Button)
    {
    case MenuButton::Prev:
        TurnPage(slot, -1);
        return;
    case MenuButton::Next:
        TurnPage(slot, +1);
        return;
    default:
        break;
    }

    if (press.Row < 0 || press.Row >= RowsPerPage)
        return;

    const int index = ItemIndex(slot, press.Row);
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

    const int pages = PageCount(static_cast<int>(menu->Items.size()), RowsPerPage);
    _pages[slot] = ((_pages[slot] + delta) % pages + pages) % pages;
}

void PanoramaDriver::Present(int slot)
{
    auto* menu = Current(slot);
    if (!menu)
        return;

    if (!_panel.Ensure(slot))
        return;

    BindClicks();

    const int items = static_cast<int>(menu->Items.size());
    const int pages = PageCount(items, RowsPerPage);
    int& page = _pages[slot];
    page = std::clamp(page, 0, pages - 1);

    // Every write below discards its Status: the panel logs the first failure for a slot itself,
    // and the next frame redraws anyway.
    (void)_panel.Text(slot, RootId, TitleVar, menu->Title);
    (void)_panel.Text(slot, RootId, SubtitleVar, menu->Subtitle);
    (void)_panel.Class(slot, SubtitleId, kHidden, menu->Subtitle.empty());

    const auto prompt = _services.ChatInput.GetPrompt(slot);
    (void)_panel.Class(slot, PromptId, kHidden, !prompt.has_value());
    if (prompt)
        (void)_panel.Text(slot, RootId, PromptVar, *prompt);

    const int first = page * RowsPerPage;
    const int last = std::min(items, first + RowsPerPage);

    for (int index = first; index < last; ++index)
        DrawRow(slot, index - first, menu->Items[static_cast<std::size_t>(index)]);
    HideRowsFrom(slot, last - first);

    (void)_panel.Class(slot, PagerId, kHidden, pages <= 1);
    if (pages > 1)
        (void)_panel.Text(slot, RootId, PageVar, std::format("{}/{}", page + 1, pages));

    (void)_panel.Class(slot, BackId, kHidden, Depth(slot) <= 1);

    (void)_panel.Class(slot, RootId, kHidden, false);
    (void)_panel.InputCapture(slot, true);
}

void PanoramaDriver::DrawRow(int slot, int row, const MenuItem& item)
{
    if (!item.Describe)
        return;

    const MenuRow described = item.Describe(slot);
    const RowIds& ids = _rows[static_cast<std::size_t>(row)];

    // Variables on the root panel; the labels reading them carry no ids.
    (void)_panel.Text(slot, RootId, ids.Label, described.Label);
    (void)_panel.Text(slot, RootId, ids.Value, described.Value);

    // Every kind is written, not just this row's: the row keeps whatever class it was last given
    // until something takes it off, and the write cache makes the five that do not change free.
    for (MenuRowKind kind : EnumValues<MenuRowKind>())
        (void)_panel.Class(slot, ids.Row, ClassFor(kind), kind == described.Kind);

    (void)_panel.Class(slot, ids.Row, kHidden, false);
    (void)_panel.Class(slot, ids.Row, kDisabled, !described.Enabled);
    (void)_panel.Class(slot, ids.Row, kHasValue, !described.Value.empty());
    // The same question the HTML footer asks, so both drivers read one answer.
    (void)_panel.Class(slot, ids.Row, kHasSteppers, described.Steppable);
}

void PanoramaDriver::HideRowsFrom(int slot, int row)
{
    for (int i = row < 0 ? 0 : row; i < RowsPerPage; ++i)
        (void)_panel.Class(slot, _rows[static_cast<std::size_t>(i)].Row, kHidden, true);
}

void PanoramaDriver::Dismiss(int slot)
{
    // Written straight from here rather than deferred to the next frame: presses are delivered on
    // the game frame, so a close that came from a row handler is already outside the engine's
    // inbound message path and may write to the entity.
    if (!_panel.Covers(slot))
        return;

    (void)_panel.Class(slot, RootId, kHidden, true);
    (void)_panel.InputCapture(slot, false);

    _panel.Forget(slot);
}

}  // namespace VoltMod
