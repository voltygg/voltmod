#include "Menu/PanoramaDriver.hpp"

#include <VoltMod/Core/EnumNames.hpp>
#include <algorithm>
#include <cstddef>
#include <format>

// Everything the Panorama driver writes into its layout: the dialog, the rows, and what a row
// that is not drawn this frame is told instead. Input and paging are in PanoramaDriver.cpp.

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
    (void)_panel.Class(slot, SubtitleId, Css::Hidden, menu->Subtitle.empty());

    const auto prompt = _services.ChatInput.GetPrompt(slot);
    (void)_panel.Class(slot, PromptId, Css::Hidden, !prompt.has_value());
    (void)_panel.Class(slot, RootId, Css::Prompting, prompt.has_value());
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
    (void)_panel.Class(slot, PagerId, Css::Hidden, pages <= 1);

    // Two ways to say the same thing: `Root` lets the stylesheet draw Back disabled in place,
    // and the older `Hidden` on the button itself keeps a layout that hides it working.
    const bool atRoot = Depth(slot) <= 1;
    (void)_panel.Class(slot, RootId, Css::Root, atRoot);
    (void)_panel.Class(slot, BackId, Css::Hidden, atRoot);

    (void)_panel.Class(slot, RootId, Css::KeyHints, KeyboardEnabled(slot));
    (void)_panel.Class(slot, RootId, Css::Hidden, false);
    (void)_panel.InputCapture(slot, true);
}

void PanoramaDriver::DrawRow(int slot, int row, int index)
{
    // Only while keys move it: a click-only session would leave the highlight wherever the cursor
    // happened to start, which reads as a selection the player did not make.
    WriteRow(slot, row, Describe(slot, index), KeyboardEnabled(slot) && index == Selected(slot));
}

void PanoramaDriver::DrawEmpty(int slot)
{
    WriteRow(slot, 0, MenuRow{.Label = Translate("menu.empty", "Nothing here", slot), .Kind = MenuRowKind::Text},
             false);
    HideRowsFrom(slot, 1);
}

void PanoramaDriver::WriteRow(int slot, int row, const MenuRow& described, bool selected)
{
    const RowIds& ids = _rows[static_cast<std::size_t>(row)];

    // Variables on the root panel; the labels reading them carry no ids.
    (void)_panel.Text(slot, RootId, ids.Label, described.Label);
    (void)_panel.Text(slot, RootId, ids.Value, described.Value);

    // Every kind is written, not just this row's: the row keeps whatever class it was last given
    // until something takes it off, and the write cache makes the five that do not change free.
    for (MenuRowKind kind : EnumValues<MenuRowKind>())
        (void)_panel.Class(slot, ids.Row, ClassFor(kind), kind == described.Kind);

    (void)_panel.Class(slot, ids.Row, Css::Hidden, false);
    (void)_panel.Class(slot, ids.Row, Css::Disabled, !described.Enabled);
    (void)_panel.Class(slot, ids.Row, Css::HasValue, !described.Value.empty());
    // Only a Choice cycles: a toggle is a switch, and drawing arrows either side of it would say
    // there is a list behind it. A/D still flips it.
    (void)_panel.Class(slot, ids.Row, Css::HasSteppers, described.Kind == MenuRowKind::Choice);
    (void)_panel.Class(slot, ids.Row, Css::On, described.State.value_or(false));
    (void)_panel.Class(slot, ids.Row, Css::Changed, described.Changed);
    (void)_panel.Class(slot, ids.Row, Css::Pending, described.Pending);
    (void)_panel.Class(slot, ids.Row, Css::Selected, selected);
}

void PanoramaDriver::HideRowsFrom(int slot, int row)
{
    for (int i = row < 0 ? 0 : row; i < RowsPerPageCount; ++i)
        (void)_panel.Class(slot, _rows[static_cast<std::size_t>(i)].Row, Css::Hidden, true);
}

}  // namespace VoltMod
