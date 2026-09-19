#include <VoltMod/Menu/PanoramaMenuLayout.hpp>
#include <cstddef>
#include <format>
#include <utility>

namespace VoltMod
{

// A draw ignores write failures: the screen logs the first one per slot and the next redraw retries.
static void Text(Screen& screen, int slot, std::string_view variable, std::string_view value)
{
    static_cast<void>(screen.SetText(slot, variable, value));
}

static void Class(Screen& screen, int slot, std::string_view elementId, std::string_view className, bool on)
{
    static_cast<void>(screen.SetClass(slot, elementId, className, on));
}

static void Hidden(Screen& screen, int slot, std::string_view elementId, bool hidden)
{
    static_cast<void>(screen.SetHidden(slot, elementId, hidden));
}

static void Cursor(Screen& screen, int slot, bool shown)
{
    static_cast<void>(screen.ShowCursor(slot, shown));
}

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
      _pageNext(std::format("{}_page_next", screen))
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

    for (std::string_view name : iconNames)
        _icons.push_back(Icon{.Name = std::string(name), .Class = std::format("Icon--{}", name)});
}

bool PanoramaMenuLayout::Show(int slot)
{
    Screen& screen = _screens.For(slot);
    if (!screen.EnsureSpawned(slot))
        return false;

    Hidden(screen, slot, _root, false);
    Cursor(screen, slot, true);
    return true;
}

void PanoramaMenuLayout::Hide(int slot)
{
    Screen* screen = _screens.Find(slot);
    if (!screen || !*screen)
        return;

    Cursor(*screen, slot, false);
    Hidden(*screen, slot, _root, true);
}

void PanoramaMenuLayout::SetHeader(int slot, const MenuHeader& header)
{
    Screen& screen = _screens.For(slot);
    Text(screen, slot, "brand", header.Brand);
    Text(screen, slot, "brand_subtitle", header.BrandSubtitle);
    Text(screen, slot, "breadcrumb", header.Breadcrumb);
    Text(screen, slot, "title", header.Title);
    Text(screen, slot, "subtitle", header.Subtitle);
    Hidden(screen, slot, _subtitle, header.Subtitle.empty());
    Class(screen, slot, _root, "HomePage", header.IsRoot);

    for (const ScreenText& text : _texts)
        Text(screen, slot, text.Variable, text.Value(slot));
}

void PanoramaMenuLayout::SetSidebarVisible(int slot, bool visible)
{
    Class(_screens.For(slot), slot, _root, "NoSidebar", !visible);
}

void PanoramaMenuLayout::SetTab(int slot, int index, const MenuTab* tab)
{
    const TabIds& ids = _tabs[static_cast<std::size_t>(index)];
    Screen& screen = _screens.For(slot);

    Hidden(screen, slot, ids.Id, !tab);
    if (!tab)
        return;

    Text(screen, slot, ids.LabelVar, tab->Label);
    Class(screen, slot, ids.Id, "Selected", tab->Selected);

    // Every icon class is written, so the one a previous tab showed turns off.
    for (const Icon& icon : _icons)
        Class(screen, slot, ids.Icon, icon.Class, icon.Name == tab->Icon);
}

void PanoramaMenuLayout::SetRow(int slot, int index, const MenuRow* row, std::string_view pendingHint)
{
    const RowIds& ids = _rows[static_cast<std::size_t>(index)];
    Screen& screen = _screens.For(slot);

    Hidden(screen, slot, ids.Id, !row);
    if (!row)
        return;

    const bool toggle = row->Kind == MenuRowKind::Toggle;
    Text(screen, slot, ids.LabelVar, row->Label);
    Text(screen, slot, ids.ValueVar, row->Value);
    Class(screen, slot, ids.Id, "HasValue", !row->Value.empty());
    Class(screen, slot, ids.Id, "Disabled", !row->Enabled);
    Class(screen, slot, ids.Id, "On", row->State.value_or(false));
    Class(screen, slot, ids.Id, "Toggle", toggle);
    Class(screen, slot, ids.Id, "HasChevron", row->Kind == MenuRowKind::Submenu || row->Kind == MenuRowKind::Input);
    // An inert row still ships a live button, so without this it hovers and clicks like any other.
    Class(screen, slot, ids.Id, "Static", !row->Selectable);
    Class(screen, slot, ids.Id, "HasSteppers", row->Steppable && row->Enabled && !toggle);
    Class(screen, slot, ids.Id, "Pending", row->Pending);
    if (row->Pending)
        Text(screen, slot, ids.HintVar, pendingHint);
}

void PanoramaMenuLayout::SetEmpty(int slot, std::string_view text)
{
    Screen& screen = _screens.For(slot);
    Text(screen, slot, "empty", text);
    Hidden(screen, slot, _empty, text.empty());
}

void PanoramaMenuLayout::SetPager(int slot, std::string_view text)
{
    Screen& screen = _screens.For(slot);
    Text(screen, slot, "page", text);
    Hidden(screen, slot, _page, text.empty());
}

void PanoramaMenuLayout::SetPrompt(int slot, std::string_view text, std::string_view hint)
{
    Screen& screen = _screens.For(slot);
    Text(screen, slot, "prompt_text", text);
    Text(screen, slot, "prompt_hint", hint);
    Hidden(screen, slot, _prompt, text.empty());
    Class(screen, slot, _root, "Prompting", !text.empty());
}

void PanoramaMenuLayout::SetFooter(int slot, std::string_view back, std::string_view cancel)
{
    Screen& screen = _screens.For(slot);
    Text(screen, slot, "back", back);
    Hidden(screen, slot, _back, back.empty());
    Text(screen, slot, "cancel", cancel);
}

void PanoramaMenuLayout::AddText(std::string variable, std::function<std::string(int slot)> text)
{
    _texts.push_back(ScreenText{.Variable = std::move(variable), .Value = std::move(text)});
}

std::optional<MenuButton> PanoramaMenuLayout::ButtonFor(std::string_view id) const
{
    if (id == _cancel)
        return MenuButton{MenuButtonKind::Cancel};
    if (id == _back)
        return MenuButton{MenuButtonKind::Back};
    if (id == _close)
        return MenuButton{MenuButtonKind::Close};
    if (id == _pagePrevious)
        return MenuButton{MenuButtonKind::PreviousPage};
    if (id == _pageNext)
        return MenuButton{MenuButtonKind::NextPage};

    for (int index = 0; index < TabCount(); ++index)
    {
        if (id == _tabs[static_cast<std::size_t>(index)].Id)
            return MenuButton{MenuButtonKind::Tab, index};
    }

    for (int index = 0; index < RowCount(); ++index)
    {
        const RowIds& row = _rows[static_cast<std::size_t>(index)];
        if (id == row.Button)
            return MenuButton{MenuButtonKind::Row, index};
        if (id == row.Decrease)
            return MenuButton{MenuButtonKind::StepDown, index};
        if (id == row.Increase)
            return MenuButton{MenuButtonKind::StepUp, index};
    }

    return std::nullopt;
}

}  // namespace VoltMod
