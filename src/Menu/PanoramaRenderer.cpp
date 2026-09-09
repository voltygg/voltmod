#include "Menu/PanoramaRenderer.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <algorithm>
#include <cstddef>

namespace VoltMod
{

PanoramaRenderer::PanoramaRenderer(const MenuServices& services, MenuCore& core, MenuSession& session,
                                   const PanoramaMenuOptions& options)
    : _services(services), _core(core), _session(session), _spec(DefaultMenuLayout)
{
    // The layout has a fixed pool of rows and tabs; asking for more would spell ids it has not got.
    _spec.Layout = options.Layout;
    _spec.RowsPerPage = std::clamp(options.Rows, 1, DefaultMenuLayout.RowsPerPage);
    _spec.NavCount = std::clamp(options.Nav, 0, DefaultMenuLayout.NavCount);

    for (int row = 0; row < _spec.RowsPerPage; ++row)
        _rows.push_back(RowIds(_spec, row));

    for (int tab = 0; tab < _spec.NavCount; ++tab)
    {
        _navIds.push_back(NavId(_spec, tab));
        _navVars.push_back(NavVar(_spec, tab));
    }

    _pages.BindReset(services.Slots);
    _panels.BindReset(services.Slots);
}

PanoramaRenderer::~PanoramaRenderer() = default;

bool PanoramaRenderer::Attach(int slot)
{
    if (!PanelFor(slot).Ensure(slot))
        return false;

    _pages[slot] = 0;
    return true;
}

void PanoramaRenderer::ShowPage(int slot, int page)
{
    _pages[slot] = page < 0 ? 0 : page;
}

void PanoramaRenderer::Dismiss(int slot)
{
    // Safe to write from here: presses arrive on the game frame, outside the engine's inbound
    // message path. The capture goes before the entity does, so nobody is left holding one.
    UiPanel& panel = _panels[slot];
    if (panel.Covers(slot))
    {
        (void)panel.Class(slot, _spec.Root, MenuCss::Hidden, true);
        (void)panel.InputCapture(slot, false);
    }

    // A closed menu costs nothing: this removes the entity and its Visibility entry, and the next
    // Attach makes a fresh panel.
    _panels[slot] = {};
}

UiPanel& PanoramaRenderer::PanelFor(int slot)
{
    UiPanel& panel = _panels[slot];
    if (panel.Viewer() == slot)
        return panel;

    auto made = _services.Ui.Panel(_spec.Layout, slot);
    if (made)
        panel = std::move(*made);
    else
        Log::Warn("Menu: no private panel for slot {} ({}).", slot, made.error().Detail);

    return panel;
}

void PanoramaRenderer::BindClicks()
{
    if (_clicks)
        return;

    _clicks = _services.Ui.Clicked += [this](const UiClick& click) { OnClick(click); };
}

void PanoramaRenderer::OnClick(const UiClick& click)
{
    const int slot = click.Slot;
    if (!IsValidSlot(slot) || !_core.Menus().Current(slot))
        return;

    // Only presses on this player's own panel, which a player drawn to elsewhere has not got.
    const EntityRef own = _panels[slot].Ref();
    if (!own || click.Layout != own)
        return;

    const MenuPress press = ParseMenuButton(_spec, click.ButtonId);

    // Prompts honor Cancel; Back and Close remain available to leave the menu.
    switch (press.Button)
    {
    case MenuButton::None:
        return;
    case MenuButton::Cancel:
        _core.CancelPrompt(slot);
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

    if (press.Button == MenuButton::Nav)
    {
        OnNavClick(slot, press.Row);
        return;
    }

    // Remaining presses are row presses and carry a row of the current page.
    if (press.Row < 0 || press.Row >= RowsPerPage())
        return;

    // Keep keyboard navigation at the clicked row.
    const int index = ItemIndex(slot, press.Row);
    ActiveMenus& menus = _core.Menus();
    menus.Select(slot, index);

    switch (press.Button)
    {
    case MenuButton::Row:
        menus.Activate(slot, index);
        break;
    case MenuButton::RowDec:
        (void)menus.Step(slot, index, -1);
        break;
    case MenuButton::RowInc:
        (void)menus.Step(slot, index, +1);
        break;
    default:
        break;
    }
}

void PanoramaRenderer::OnNavClick(int slot, int tab)
{
    const std::vector<int> items = NavRows(slot);
    if (tab < 0 || tab >= static_cast<int>(items.size()))
        return;

    // Back to the root without taking the menu off the screen, then open that tab.
    while (_core.Menus().Depth(slot) > 1)
        _session.Close(slot);

    ActiveMenus& menus = _core.Menus();
    const int index = items[static_cast<std::size_t>(tab)];
    menus.Select(slot, index);
    _pages[slot] = index / RowsPerPage();
    menus.Activate(slot, index);
}

void PanoramaRenderer::TurnPage(int slot, int delta)
{
    auto* menu = _core.Menus().Current(slot);
    if (!menu)
        return;

    const int pages = PageCount(static_cast<int>(menu->Items.size()), RowsPerPage());
    _pages[slot] = WrapIndex(_pages[slot] + delta, pages);
    // Keep the cursor on the page being drawn.
    _core.Menus().SelectOnPage(slot, _pages[slot], RowsPerPage());
}

std::vector<int> PanoramaRenderer::NavRows(int slot)
{
    std::vector<int> rows;
    if (_spec.NavCount <= 0 || !IsValidSlot(slot))
        return rows;

    const auto& stack = _core.Menus().State(slot).MenuStack;
    if (stack.empty())
        return rows;

    // The tabs are the root menu's submenu rows, which is the stack's bottom whatever is on top.
    const Menu& root = *stack.front();
    for (std::size_t index = 0; index < root.Items.size() && rows.size() < _navIds.size(); ++index)
    {
        const auto& describe = root.Items[index].Describe;
        if (describe && describe(slot).Kind == MenuRowKind::Submenu)
            rows.push_back(static_cast<int>(index));
    }
    return rows;
}

}  // namespace VoltMod
