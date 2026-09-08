#include "Menu/PanoramaDriver.hpp"

#include <VoltMod/Core/EnumNames.hpp>
#include <algorithm>
#include <cstddef>
#include <format>

namespace VoltMod
{

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

bool PanoramaDriver::Present(int slot)
{
    auto* menu = _menus.Current(slot);
    if (!menu)
        return true;

    UiPanel& panel = PanelFor(slot);
    if (!panel.Ensure(slot))
        return false;

    BindClicks();

    const int items = static_cast<int>(menu->Items.size());
    const int pages = PageCount(items, RowsPerPageCount);
    int& page = _pages[slot];
    page = std::clamp(page, 0, pages - 1);

    // The panel logs write failures; the next frame redraws the state.
    (void)panel.Text(slot, RootId, TitleVar, menu->Title);
    (void)panel.Text(slot, RootId, SubtitleVar, menu->Subtitle);
    (void)panel.Text(slot, RootId, BreadcrumbVar, _menus.Breadcrumb(slot));
    (void)panel.Class(slot, SubtitleId, Css::Hidden, menu->Subtitle.empty());

    const auto prompt = _services.ChatInput.GetPrompt(slot);
    (void)panel.Class(slot, PromptId, Css::Hidden, !prompt.has_value());
    (void)panel.Class(slot, RootId, Css::Prompting, prompt.has_value());
    if (prompt)
    {
        (void)panel.Text(slot, RootId, PromptVar, *prompt);
        (void)panel.Text(slot, RootId, PromptHintVar, _session.Translate(slot, "menu.promptHint", "Answer in chat"));
    }

    if (items == 0)
    {
        DrawEmpty(panel, slot);
    }
    else
    {
        const int first = page * RowsPerPageCount;
        const int last = std::min(items, first + RowsPerPageCount);
        for (int index = first; index < last; ++index)
            DrawRow(panel, slot, index - first, index);
        HideRowsFrom(panel, slot, last - first);
    }

    // Always written, so a layout may show the counter next to the title rather than inside the
    // pager the second page is what unhides.
    (void)panel.Text(slot, RootId, PageVar, std::format("{}/{}", page + 1, pages));
    (void)panel.Class(slot, PagerId, Css::Hidden, pages <= 1);

    // Set both properties for compatibility with layouts that hide the button.
    const bool atRoot = _menus.Depth(slot) <= 1;
    (void)panel.Class(slot, RootId, Css::Root, atRoot);
    (void)panel.Class(slot, BackId, Css::Hidden, atRoot);

    (void)panel.Class(slot, RootId, Css::KeyHints, _menus.KeyboardEnabled(slot));
    (void)panel.Class(slot, RootId, Css::Hidden, false);
    (void)panel.InputCapture(slot, true);
    return true;
}

void PanoramaDriver::DrawRow(UiPanel& panel, int slot, int row, int index)
{
    // Only while keys move it: a click-only session would leave the highlight wherever the cursor
    // happened to start, which reads as a selection the player did not make.
    WriteRow(panel, slot, row, _menus.Describe(slot, index),
             _menus.KeyboardEnabled(slot) && index == _menus.Selected(slot));
}

void PanoramaDriver::DrawEmpty(UiPanel& panel, int slot)
{
    WriteRow(panel, slot, 0,
             MenuRow{.Label = _session.Translate(slot, "menu.empty", "Nothing here"), .Kind = MenuRowKind::Text},
             false);
    HideRowsFrom(panel, slot, 1);
}

void PanoramaDriver::WriteRow(UiPanel& panel, int slot, int row, const MenuRow& described, bool selected)
{
    const RowIds& ids = _rows[static_cast<std::size_t>(row)];

    // Variables live on the root panel; labels resolve them through their ancestors.
    (void)panel.Text(slot, RootId, ids.Label, described.Label);
    (void)panel.Text(slot, RootId, ids.Value, described.Value);

    // Every kind is written, not just this row's: the row keeps whatever class it was last given
    // until something takes it off, and the write cache makes the five that do not change free.
    for (MenuRowKind kind : EnumValues<MenuRowKind>())
        (void)panel.Class(slot, ids.Row, ClassFor(kind), kind == described.Kind);

    (void)panel.Class(slot, ids.Row, Css::Hidden, false);
    (void)panel.Class(slot, ids.Row, Css::Disabled, !described.Enabled);
    (void)panel.Class(slot, ids.Row, Css::HasValue, !described.Value.empty());
    // Only a Choice cycles: a toggle is a switch, and drawing arrows either side of it would say
    // there is a list behind it. A/D still flips it.
    (void)panel.Class(slot, ids.Row, Css::HasSteppers, described.Kind == MenuRowKind::Choice);
    (void)panel.Class(slot, ids.Row, Css::On, described.State.value_or(false));
    (void)panel.Class(slot, ids.Row, Css::Changed, described.Changed);
    (void)panel.Class(slot, ids.Row, Css::Pending, described.Pending);
    (void)panel.Class(slot, ids.Row, Css::Selected, selected);
}

void PanoramaDriver::HideRowsFrom(UiPanel& panel, int slot, int row)
{
    for (int i = row < 0 ? 0 : row; i < RowsPerPageCount; ++i)
        (void)panel.Class(slot, _rows[static_cast<std::size_t>(i)].Row, Css::Hidden, true);
}

}  // namespace VoltMod
