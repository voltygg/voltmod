#include "Menu/PanoramaRenderer.hpp"

#include <VoltMod/Core/EnumNames.hpp>
#include <algorithm>
#include <cstddef>
#include <format>

namespace VoltMod
{

bool PanoramaRenderer::Present(int slot)
{
    ActiveMenus& menus = _core.Menus();
    auto* menu = menus.Current(slot);
    if (!menu)
        return true;

    UiPanel& panel = PanelFor(slot);
    if (!panel.Ensure(slot))
        return false;

    BindClicks();

    const int items = static_cast<int>(menu->Items.size());
    const int pages = PageCount(items, RowsPerPage());
    int& page = _pages[slot];
    page = std::clamp(page, 0, pages - 1);

    // The panel logs write failures; the next frame redraws the state.
    (void)panel.Text(slot, _spec.Root, _spec.TitleVar, menu->Title);
    (void)panel.Text(slot, _spec.Root, _spec.SubtitleVar, menu->Subtitle);
    (void)panel.Text(slot, _spec.Root, _spec.BreadcrumbVar, menus.Breadcrumb(slot));
    (void)panel.Class(slot, _spec.SubtitleId, MenuCss::Hidden, menu->Subtitle.empty());

    // The buttons the layout draws itself: written every frame, deduplicated by the write cache.
    (void)panel.Text(slot, _spec.Root, _spec.NavBackVar, _core.Translate(slot, "nav.back", "Back"));
    (void)panel.Text(slot, _spec.Root, _spec.NavCloseVar, _core.Translate(slot, "nav.close", "Close"));
    (void)panel.Text(slot, _spec.Root, _spec.NavCancelVar, _core.Translate(slot, "nav.cancel", "Cancel"));

    const auto prompt = _services.ChatInput.GetPrompt(slot);
    (void)panel.Class(slot, _spec.PromptId, MenuCss::Hidden, !prompt.has_value());
    (void)panel.Class(slot, _spec.Root, MenuCss::Prompting, prompt.has_value());
    if (prompt)
    {
        (void)panel.Text(slot, _spec.Root, _spec.PromptTextVar, *prompt);
        (void)panel.Text(slot, _spec.Root, _spec.PromptHintVar,
                         _core.Translate(slot, "menu.promptHint", "Answer in chat"));
    }

    if (items == 0)
    {
        DrawEmpty(panel, slot);
    }
    else
    {
        const int first = page * RowsPerPage();
        const int last = std::min(items, first + RowsPerPage());
        for (int index = first; index < last; ++index)
            DrawRow(panel, slot, index - first, index);
        HideRowsFrom(panel, slot, last - first);
    }

    DrawNav(panel, slot);

    // Always written, so a layout may show the counter next to the title rather than inside the
    // pager the second page is what unhides.
    (void)panel.Text(slot, _spec.Root, _spec.PageVar, std::format("{}/{}", page + 1, pages));
    (void)panel.Class(slot, _spec.PagerId, MenuCss::Hidden, pages <= 1);

    // Set both properties for compatibility with layouts that hide the button.
    const bool atRoot = menus.Depth(slot) <= 1;
    (void)panel.Class(slot, _spec.Root, MenuCss::Root, atRoot);
    (void)panel.Class(slot, _spec.BackId, MenuCss::Hidden, atRoot);

    (void)panel.Class(slot, _spec.Root, MenuCss::KeyHints, menus.KeyboardEnabled(slot));
    (void)panel.Class(slot, _spec.Root, MenuCss::Hidden, false);
    (void)panel.InputCapture(slot, true);
    return true;
}

void PanoramaRenderer::DrawRow(UiPanel& panel, int slot, int row, int index)
{
    ActiveMenus& menus = _core.Menus();
    // Only while keys move it: a click-only session would leave the highlight wherever the cursor
    // happened to start, which reads as a selection the player did not make.
    WriteRow(panel, slot, row, menus.Describe(slot, index),
             menus.KeyboardEnabled(slot) && index == menus.Selected(slot));
}

void PanoramaRenderer::DrawEmpty(UiPanel& panel, int slot)
{
    WriteRow(panel, slot, 0,
             MenuRow{.Label = _core.Translate(slot, "menu.empty", "Nothing here"), .Kind = MenuRowKind::Text}, false);
    HideRowsFrom(panel, slot, 1);
}

void PanoramaRenderer::WriteRow(UiPanel& panel, int slot, int row, const MenuRow& described, bool selected)
{
    const MenuRowIds& ids = _rows[static_cast<std::size_t>(row)];

    // Variables live on the root panel; labels resolve them through their ancestors.
    (void)panel.Text(slot, _spec.Root, ids.LabelVar, described.Label);
    (void)panel.Text(slot, _spec.Root, ids.ValueVar, described.Value);

    // Every kind is written, not just this row's: the row keeps whatever class it was last given
    // until something takes it off, and the write cache makes the five that do not change free.
    for (MenuRowKind kind : EnumValues<MenuRowKind>())
        (void)panel.Class(slot, ids.Row, MenuKindClass(kind), kind == described.Kind);

    (void)panel.Class(slot, ids.Row, MenuCss::Hidden, false);
    (void)panel.Class(slot, ids.Row, MenuCss::Disabled, !described.Enabled);
    (void)panel.Class(slot, ids.Row, MenuCss::HasValue, !described.Value.empty());
    // Only a Choice cycles: a toggle is a switch, and drawing arrows either side of it would say
    // there is a list behind it. A/D still flips it.
    (void)panel.Class(slot, ids.Row, MenuCss::HasSteppers, described.Kind == MenuRowKind::Choice);
    (void)panel.Class(slot, ids.Row, MenuCss::On, described.State.value_or(false));
    (void)panel.Class(slot, ids.Row, MenuCss::Changed, described.Changed);
    (void)panel.Class(slot, ids.Row, MenuCss::Pending, described.Pending);
    (void)panel.Class(slot, ids.Row, MenuCss::Selected, selected);
}

void PanoramaRenderer::HideRowsFrom(UiPanel& panel, int slot, int row)
{
    for (std::size_t i = static_cast<std::size_t>(row < 0 ? 0 : row); i < _rows.size(); ++i)
        (void)panel.Class(slot, _rows[i].Row, MenuCss::Hidden, true);
}

void PanoramaRenderer::DrawNav(UiPanel& panel, int slot)
{
    if (_navIds.empty())
        return;

    const auto& stack = _core.Menus().State(slot).MenuStack;
    if (stack.empty())
        return;

    // The tab standing for the menu opened off the root, matched on its plain title: that is the
    // text the row which opened it carries.
    const std::string_view open = stack.size() > 1 ? std::string_view(stack[1]->Title) : std::string_view{};
    const Menu& root = *stack.front();
    const std::vector<int> items = NavRows(slot);

    std::size_t tab = 0;
    for (; tab < items.size(); ++tab)
    {
        const MenuRow row = root.Items[static_cast<std::size_t>(items[tab])].Describe(slot);
        (void)panel.Text(slot, _spec.Root, _navVars[tab], row.Label);
        (void)panel.Class(slot, _navIds[tab], MenuCss::Hidden, false);
        (void)panel.Class(slot, _navIds[tab], MenuCss::Disabled, !row.Enabled);
        (void)panel.Class(slot, _navIds[tab], MenuCss::Selected, !open.empty() && row.Label == open);
    }

    for (; tab < _navIds.size(); ++tab)
        (void)panel.Class(slot, _navIds[tab], MenuCss::Hidden, true);
}

}  // namespace VoltMod
