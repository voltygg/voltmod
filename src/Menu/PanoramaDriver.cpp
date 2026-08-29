#include "Menu/PanoramaDriver.hpp"

#include "Menu/PanoramaIds.hpp"

#include <VoltMod/Core/EnumNames.hpp>
#include <algorithm>
#include <format>
#include <initializer_list>
#include <utility>

namespace VoltMod
{

// The class vocabulary a stylesheet styles against. Every one of them is written on every draw,
// on or off, so a row never keeps a class the state it stood for has left - and the panel's write
// cache makes the ones that did not change free.
static constexpr std::string_view kHidden = "Hidden";
static constexpr std::string_view kDisabled = "Disabled";
static constexpr std::string_view kSelected = "Selected";
static constexpr std::string_view kChanged = "Changed";
static constexpr std::string_view kPending = "Pending";
static constexpr std::string_view kHasValue = "HasValue";
static constexpr std::string_view kHasSteppers = "HasSteppers";
static constexpr std::string_view kOn = "On";
static constexpr std::string_view kPrompting = "Prompting";
static constexpr std::string_view kKeyHints = "KeyHints";
static constexpr std::string_view kRoot = "Root";

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

    if (press.Row < 0 || press.Row >= RowsPerPageCount)
        return;

    const int index = ItemIndex(slot, press.Row);
    switch (press.Button)
    {
    case MenuButton::Row:
        // A click moves the cursor there too, so the keyboard carries on from what was pressed
        // rather than from wherever it was left.
        Select(slot, index);
        Activate(slot, index);
        break;
    case MenuButton::RowDec:
        Select(slot, index);
        (void)Step(slot, index, -1);
        break;
    case MenuButton::RowInc:
        Select(slot, index);
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
    _pages[slot] = ((_pages[slot] + delta) % pages + pages) % pages;
    // The cursor follows the page rather than sitting on a row that is no longer drawn.
    SelectOnPage(slot, _pages[slot]);
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
    const int pages = PageCount(items, RowsPerPageCount);
    int& page = _pages[slot];
    page = std::clamp(page, 0, pages - 1);

    // Every write below discards its Status: the panel logs the first failure for a slot itself,
    // and the next frame redraws anyway.
    (void)_panel.Text(slot, RootId, TitleVar, menu->Title);
    (void)_panel.Text(slot, RootId, SubtitleVar, menu->Subtitle);
    (void)_panel.Text(slot, RootId, CrumbsVar, Crumbs(slot));
    (void)_panel.Class(slot, SubtitleId, kHidden, menu->Subtitle.empty());

    const auto prompt = _services.ChatInput.GetPrompt(slot);
    (void)_panel.Class(slot, PromptId, kHidden, !prompt.has_value());
    (void)_panel.Class(slot, RootId, kPrompting, prompt.has_value());
    if (prompt)
    {
        (void)_panel.Text(slot, RootId, PromptVar, *prompt);
        (void)_panel.Text(slot, RootId, PromptHintVar, Translate("menu.promptHint", "Answer in chat", slot));
    }

    if (items == 0)
    {
        DrawEmpty(slot);
    }
    else
    {
        const int first = page * RowsPerPageCount;
        const int last = std::min(items, first + RowsPerPageCount);
        for (int index = first; index < last; ++index)
            DrawRow(slot, index - first, index);
        HideRowsFrom(slot, last - first);
    }

    // Always written, so a layout may show the counter next to the title rather than inside the
    // pager the second page is what unhides.
    (void)_panel.Text(slot, RootId, PageVar, std::format("{}/{}", page + 1, pages));
    (void)_panel.Class(slot, PagerId, kHidden, pages <= 1);

    // Two ways to say the same thing: `Root` lets the stylesheet draw Back disabled in place,
    // and the older `Hidden` on the button itself keeps a layout that hides it working.
    const bool atRoot = Depth(slot) <= 1;
    (void)_panel.Class(slot, RootId, kRoot, atRoot);
    (void)_panel.Class(slot, BackId, kHidden, atRoot);

    (void)_panel.Class(slot, RootId, kKeyHints, KeyboardEnabled(slot));
    (void)_panel.Class(slot, RootId, kHidden, false);
    (void)_panel.InputCapture(slot, true);
}

void PanoramaDriver::DrawRow(int slot, int row, int index)
{
    const MenuRow described = Describe(slot, index);
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
    // Only a Choice cycles: a toggle is a switch, and drawing arrows either side of it would say
    // there is a list behind it. A/D still flips it.
    (void)_panel.Class(slot, ids.Row, kHasSteppers, described.Kind == MenuRowKind::Choice);
    (void)_panel.Class(slot, ids.Row, kOn, described.State.value_or(false));
    (void)_panel.Class(slot, ids.Row, kChanged, described.Changed);
    (void)_panel.Class(slot, ids.Row, kPending, described.Pending);
    // Only while keys move it: a click-only session would leave the highlight wherever the cursor
    // happened to start, which reads as a selection the player did not make.
    (void)_panel.Class(slot, ids.Row, kSelected, KeyboardEnabled(slot) && index == Selected(slot));
}

void PanoramaDriver::DrawEmpty(int slot)
{
    const RowIds& ids = _rows.front();

    (void)_panel.Text(slot, RootId, ids.Label, Translate("menu.empty", "Nothing here", slot));
    (void)_panel.Text(slot, RootId, ids.Value, "");

    for (MenuRowKind kind : EnumValues<MenuRowKind>())
        (void)_panel.Class(slot, ids.Row, ClassFor(kind), kind == MenuRowKind::Text);

    for (std::string_view state : {kDisabled, kHasValue, kHasSteppers, kOn, kChanged, kPending, kSelected})
        (void)_panel.Class(slot, ids.Row, state, false);

    (void)_panel.Class(slot, ids.Row, kHidden, false);
    HideRowsFrom(slot, 1);
}

void PanoramaDriver::HideRowsFrom(int slot, int row)
{
    for (int i = row < 0 ? 0 : row; i < RowsPerPageCount; ++i)
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
