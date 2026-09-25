#include <VoltMod/Menu/PanoramaMenuLayout.hpp>
#include <cstddef>
#include <format>
#include <utility>

namespace VoltMod
{

PanoramaMenuLayout::PanoramaMenuLayout(ScreenManager& screens, std::string_view screen, std::size_t tabs,
                                       std::size_t rows, std::span<const std::string_view> iconNames)
    : _screens(screens, std::string(screen)),
      _root(screen),
      _subtitle(std::format("{}_subtitle", screen)),
      _close(std::format("{}_close", screen)),
      _empty(std::format("{}_empty", screen)),
      _prompt(std::format("{}_prompt", screen)),
      _cancel(std::format("{}_cancel", screen)),
      _back(std::format("{}_back", screen)),
      _page(std::format("{}_page", screen)),
      _pagePrevious(std::format("{}_page_previous", screen)),
      _pageNext(std::format("{}_page_next", screen)),
      _iconNames(iconNames)
{
    for (std::size_t index = 0; index < tabs; ++index)
    {
        _tabs.push_back(TabIds{
            .Id = std::format("{}_tab{}", screen, index),
            .Icon = std::format("{}_tab{}_icon", screen, index),
            .LabelVar = std::format("tab{}", index),
        });
    }

    for (std::size_t index = 0; index < rows; ++index)
    {
        _rows.push_back(RowIds{
            .Id = std::format("{}_row{}", screen, index),
            .Button = std::format("{}_row{}_button", screen, index),
            .Decrease = std::format("{}_row{}_decrease", screen, index),
            .Increase = std::format("{}_row{}_increase", screen, index),
            .LabelVar = std::format("row{}_label", index),
            .HintVar = std::format("row{}_hint", index),
            .ValueVar = std::format("row{}_value", index),
        });
    }
}

bool PanoramaMenuLayout::Show(int slot)
{
    Screen& screen = _screens.For(slot);
    if (!screen.EnsureSpawned(slot))
    {
        return false;
    }

    screen.SetHidden(slot, _root, false);
    screen.ShowCursor(slot, true);
    return true;
}

void PanoramaMenuLayout::Hide(int slot)
{
    Screen* screen = _screens.Find(slot);
    if (!screen || !*screen)
    {
        return;
    }

    // Clients draw only their first custom_hud_layout, so a hidden one would block the next menu.
    screen->ShowCursor(slot, false);
    screen->Remove();
}

void PanoramaMenuLayout::SetHeader(int slot, const MenuHeader& header)
{
    Screen& screen = _screens.For(slot);
    screen.SetText(slot, "brand", header.Brand);
    screen.SetText(slot, "brand_subtitle", header.BrandSubtitle);
    screen.SetText(slot, "breadcrumb", header.Breadcrumb);
    screen.SetText(slot, "title", header.Title);
    screen.SetText(slot, "subtitle", header.Subtitle);
    screen.SetHidden(slot, _subtitle, header.Subtitle.empty());

    for (const ScreenText& text : _texts)
    {
        screen.SetText(slot, text.Variable, text.Value(slot));
    }
}

void PanoramaMenuLayout::SetSidebarVisible(int slot, bool visible)
{
    _screens.For(slot).SetClass(slot, _root, "screen--no-sidebar", !visible);
}

void PanoramaMenuLayout::SetHomeVisible(int slot, bool visible)
{
    _screens.For(slot).SetClass(slot, _root, "screen--home", visible);
}

void PanoramaMenuLayout::SetTab(int slot, int index, const MenuTab* tab)
{
    const TabIds& ids = _tabs[static_cast<std::size_t>(index)];
    Screen& screen = _screens.For(slot);

    screen.SetHidden(slot, ids.Id, !tab);
    if (!tab)
    {
        return;
    }

    screen.SetText(slot, ids.LabelVar, tab->Label);
    screen.SetClass(slot, ids.Id, "tab--selected", tab->Selected);

    screen.ShowIcon(slot, ids.Icon, _iconNames, tab->Icon);
}

void PanoramaMenuLayout::SetRow(int slot, int index, const MenuRow* row, std::string_view pendingHint)
{
    const RowIds& ids = _rows[static_cast<std::size_t>(index)];
    Screen& screen = _screens.For(slot);

    screen.SetHidden(slot, ids.Id, !row);
    if (!row)
    {
        return;
    }

    const bool toggle = row->Kind == MenuRowKind::Toggle;
    screen.SetText(slot, ids.LabelVar, row->Label);
    screen.SetText(slot, ids.ValueVar, row->Value);
    screen.SetClass(slot, ids.Id, "row--value", !row->Value.empty());
    screen.SetClass(slot, ids.Id, "row--disabled", !row->Enabled);
    screen.SetClass(slot, ids.Id, "row--on", row->State.value_or(false));
    screen.SetClass(slot, ids.Id, "row--toggle", toggle);
    screen.SetClass(slot, ids.Id, "row--chevron", row->Kind == MenuRowKind::Submenu || row->Kind == MenuRowKind::Input);
    // An inert row still ships a live button, so without this it hovers and clicks like any other.
    screen.SetClass(slot, ids.Id, "row--static", !row->Selectable);
    screen.SetClass(slot, ids.Id, "row--steppers", row->Steppable && row->Enabled && !toggle);
    screen.SetClass(slot, ids.Id, "row--pending", row->Pending);
    if (row->Pending)
    {
        screen.SetText(slot, ids.HintVar, pendingHint);
    }
}

void PanoramaMenuLayout::SetEmpty(int slot, std::string_view text)
{
    Screen& screen = _screens.For(slot);
    screen.SetText(slot, "empty", text);
    screen.SetHidden(slot, _empty, text.empty());
}

void PanoramaMenuLayout::SetPager(int slot, std::string_view text)
{
    Screen& screen = _screens.For(slot);
    screen.SetText(slot, "page", text);
    screen.SetHidden(slot, _page, text.empty());
}

void PanoramaMenuLayout::SetPrompt(int slot, std::string_view text, std::string_view hint)
{
    Screen& screen = _screens.For(slot);
    screen.SetText(slot, "prompt_text", text);
    screen.SetText(slot, "prompt_hint", hint);
    screen.SetHidden(slot, _prompt, text.empty());
    screen.SetClass(slot, _root, "screen--prompting", !text.empty());
}

void PanoramaMenuLayout::SetFooter(int slot, std::string_view back, std::string_view cancel)
{
    Screen& screen = _screens.For(slot);
    screen.SetText(slot, "back", back);
    screen.SetHidden(slot, _back, back.empty());
    screen.SetText(slot, "cancel", cancel);
}

void PanoramaMenuLayout::AddText(std::string_view variable, std::function<std::string(int slot)> text)
{
    _texts.push_back(ScreenText{.Variable = std::string{variable}, .Value = std::move(text)});
}

std::optional<MenuButton> PanoramaMenuLayout::ButtonFor(std::string_view id) const
{
    if (id == _cancel)
    {
        return MenuButton{MenuButtonKind::Cancel};
    }
    if (id == _back)
    {
        return MenuButton{MenuButtonKind::Back};
    }
    if (id == _close)
    {
        return MenuButton{MenuButtonKind::Close};
    }
    if (id == _pagePrevious)
    {
        return MenuButton{MenuButtonKind::PreviousPage};
    }
    if (id == _pageNext)
    {
        return MenuButton{MenuButtonKind::NextPage};
    }

    for (int index = 0; index < TabCount(); ++index)
    {
        if (id == _tabs[static_cast<std::size_t>(index)].Id)
        {
            return MenuButton{MenuButtonKind::Tab, index};
        }
    }

    for (int index = 0; index < RowCount(); ++index)
    {
        const RowIds& row = _rows[static_cast<std::size_t>(index)];
        if (id == row.Button)
        {
            return MenuButton{MenuButtonKind::Row, index};
        }
        if (id == row.Decrease)
        {
            return MenuButton{MenuButtonKind::StepDown, index};
        }
        if (id == row.Increase)
        {
            return MenuButton{MenuButtonKind::StepUp, index};
        }
    }

    return std::nullopt;
}

}  // namespace VoltMod
