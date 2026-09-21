#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Menu/PanoramaMenu.hpp>
#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

namespace VoltMod
{

PanoramaMenu::PanoramaMenu(const Services& services, MenuLayout& layout, uint64_t addonId)
    : _services(services),
      _layout(layout),
      _stack(*this, _services.Translations, services.Scheduler, services.Slots),
      _sessions(services.Slots)
{
    // Without the addon, report that the layout is unavailable instead of drawing blanks.
    if (addonId == 0)
        Log::Warn("PanoramaMenu: no addon required. Only a client the layout was compiled into can see it.");
    else if (auto required = _services.Addons.Require(addonId))
        _addon = std::move(*required);
    else
        Log::Warn("PanoramaMenu: addon {} not required ({}); clients without the layout will see nothing.", addonId,
                  required.error().Detail);

    _subs.Add(_services.Screens.Pressed += [this](const ButtonPress& press) { OnPress(press); });

    // A held commit is applied by a timer, so redraw it here.
    _subs.Add(_stack.Committed += [this](int slot) { Draw(slot); });
}

bool PanoramaMenu::CanShow(int slot) const
{
    // A client still fetching the addon has no layout to draw.
    return IsValidSlot(slot) && _services.Screens.Available() && !_services.Addons.HasMissing(slot);
}

bool PanoramaMenu::OpenSession(int slot, std::shared_ptr<Menu> menu, MenuOptions options)
{
    if (!menu || !CanShow(slot))
        return false;

    CloseAll(slot);
    _stack.Push(slot, std::move(menu));
    _sessions[slot].HomePage = options.HomePage;
    ReadTabs(slot);

    // Close the session when the layout cannot be shown.
    Draw(slot);
    if (!IsOpen(slot))
        return false;

    _services.Freeze.Open(slot, options.FreezeMovement);
    return true;
}

void PanoramaMenu::Open(int slot, std::shared_ptr<Menu> menu)
{
    if (!menu)
        return;
    if (!IsOpen(slot))
    {
        OpenSession(slot, std::move(menu), {});
        return;
    }

    _stack.Push(slot, std::move(menu));
    _sessions[slot].Page = 0;
    Draw(slot);
}

bool PanoramaMenu::IsOpen(int slot) const
{
    return _stack.IsOpen(slot);
}

void PanoramaMenu::Close(int slot)
{
    if (!IsOpen(slot))
        return;

    _services.ChatInput.CancelCapture(slot);
    _stack.Pop(slot);
    if (!IsOpen(slot))
        return Hide(slot);

    Session& session = _sessions[slot];
    session.Page = 0;
    if (_stack.Depth(slot) == 1)
        session.SelectedTab = -1;
    Draw(slot);
}

void PanoramaMenu::CloseAll(int slot)
{
    if (!IsOpen(slot))
        return;

    _stack.Clear(slot);
    Hide(slot);
}

void PanoramaMenu::CloseAll(int slot, std::string_view replyKey)
{
    // Reply before removing the player's menus.
    if (auto& reply = _services.Policy.Reply; reply)
        reply(slot, _services.Translations.Get(std::string(replyKey), slot));

    CloseAll(slot);
}

void PanoramaMenu::Prompt(int slot, std::string prompt, std::function<bool(int, std::string_view)> callback)
{
    _services.ChatInput.BeginCapture(slot, std::move(prompt), std::move(callback));
    DrawPrompt(slot);
}

std::string PanoramaMenu::Translate(int slot, std::string_view key, std::string_view fallback) const
{
    return _services.Translations.GetOr(key, slot, fallback);
}

int PanoramaMenu::ItemAt(int slot, int row) const
{
    return _sessions[slot].Page * _layout.RowCount() + row;
}

void PanoramaMenu::ReadTabs(int slot)
{
    // Cache the root's submenus for the session.
    std::vector<Tab>& tabs = _sessions[slot].Tabs;
    const int items = static_cast<int>(_stack.Root(slot)->Items.size());
    for (int index = 0; index < items && static_cast<int>(tabs.size()) < _layout.TabCount(); ++index)
    {
        if (MenuRow described = _stack.Describe(slot, index); described.Kind == MenuRowKind::Submenu)
            tabs.push_back(
                {.RootIndex = index, .Label = std::move(described.Label), .Icon = std::move(described.Icon)});
    }
}

void PanoramaMenu::Draw(int slot)
{
    const Menu* menu = _stack.Current(slot);
    if (!menu)
        return;

    // This also restores the screen after a map change removes it.
    if (!_layout.Show(slot))
        return CloseAll(slot);

    // The root subtitle already appears below the brand.
    const Menu& root = *_stack.Root(slot);
    const bool isRoot = menu == &root;
    _layout.SetHeader(slot, {.Brand = root.Title,
                             .BrandSubtitle = root.Subtitle,
                             .Breadcrumb = _stack.Breadcrumb(slot),
                             .Title = menu->Title,
                             .Subtitle = isRoot ? std::string_view{} : std::string_view(menu->Subtitle)});
    _layout.SetHomeVisible(slot, isRoot && _sessions[slot].HomePage);
    DrawTabs(slot);
    DrawRows(slot, *menu);
    DrawPrompt(slot);

    _layout.SetFooter(slot, isRoot ? std::string{} : Translate(slot, "nav.back", "Back"),
                      Translate(slot, "menu.cancel", "Cancel"));
}

void PanoramaMenu::DrawTabs(int slot)
{
    const Session& session = _sessions[slot];
    const int shown = static_cast<int>(session.Tabs.size());

    // Do not render a sidebar containing only the title.
    _layout.SetSidebarVisible(slot, shown > 0);

    for (int index = 0; index < _layout.TabCount(); ++index)
    {
        if (index >= shown)
        {
            _layout.SetTab(slot, index, nullptr);
            continue;
        }

        const Tab& tab = session.Tabs[static_cast<std::size_t>(index)];
        const MenuTab drawn{.Label = tab.Label, .Icon = tab.Icon, .Selected = index == session.SelectedTab};
        _layout.SetTab(slot, index, &drawn);
    }
}

void PanoramaMenu::DrawRows(int slot, const Menu& menu)
{
    Session& session = _sessions[slot];
    const int count = static_cast<int>(menu.Items.size());
    const int pages = PageCount(count, _layout.RowCount());
    session.Page = std::clamp(session.Page, 0, pages - 1);

    std::string pendingHint;
    for (int row = 0; row < _layout.RowCount(); ++row)
    {
        const int item = ItemAt(slot, row);
        if (item >= count)
        {
            _layout.SetRow(slot, row, nullptr, {});
            continue;
        }

        const MenuRow described = _stack.Describe(slot, item);
        if (described.Pending && pendingHint.empty())
            pendingHint = Translate(slot, "menu.pending", "Applying...");
        _layout.SetRow(slot, row, &described, pendingHint);
    }

    _layout.SetEmpty(slot, count == 0 ? Translate(slot, "menu.empty", "Nothing here") : std::string{});
    _layout.SetPager(slot, pages > 1 ? std::format("{} / {}", session.Page + 1, pages) : std::string{});
}

void PanoramaMenu::DrawPrompt(int slot)
{
    const auto prompt = _services.ChatInput.GetPrompt(slot);
    _layout.SetPrompt(slot, prompt.value_or(""),
                      prompt ? Translate(slot, "menu.promptHint", "Type your answer in chat") : std::string{});
}

void PanoramaMenu::OnPress(const ButtonPress& press)
{
    const int slot = press.Slot;
    if (!IsOpen(slot))
        return;

    const auto button = _layout.ButtonFor(press.ButtonId);
    if (!button)
        return;

    // A prompt handles presses except Cancel; its answer arrives as chat input.
    if (_services.ChatInput.IsCapturing(slot) && button->Kind != MenuButtonKind::Cancel)
        return;

    switch (button->Kind)
    {
    case MenuButtonKind::Cancel:
        _services.ChatInput.CancelCapture(slot);
        return Draw(slot);
    case MenuButtonKind::Back:
        return Close(slot);
    case MenuButtonKind::Close:
        return CloseAll(slot);
    case MenuButtonKind::PreviousPage:
        return TurnPage(slot, -1);
    case MenuButtonKind::NextPage:
        return TurnPage(slot, +1);
    case MenuButtonKind::Tab:
        return OpenTab(slot, button->Index);
    case MenuButtonKind::Row:
        return Activate(slot, ItemAt(slot, button->Index));
    case MenuButtonKind::StepDown:
        return StepRow(slot, button->Index, -1);
    case MenuButtonKind::StepUp:
        return StepRow(slot, button->Index, +1);
    }
}

void PanoramaMenu::Activate(int slot, int index)
{
    // Record the branch's tab so it stays selected.
    Session& session = _sessions[slot];
    if (_stack.Depth(slot) == 1)
    {
        const auto found = std::ranges::find(session.Tabs, index, &Tab::RootIndex);
        session.SelectedTab = found == session.Tabs.end() ? -1 : static_cast<int>(found - session.Tabs.begin());
    }

    // Activation may replace or close the session, so redraw only if it remains active.
    _stack.Activate(slot, index);
    Draw(slot);
}

void PanoramaMenu::StepRow(int slot, int row, int direction)
{
    if (_stack.Step(slot, ItemAt(slot, row), direction))
        Draw(slot);
}

void PanoramaMenu::OpenTab(int slot, int tab)
{
    Session& session = _sessions[slot];
    if (tab < 0 || tab >= static_cast<int>(session.Tabs.size()))
        return;

    // A tab jumps from the root instead of pushing another menu.
    _stack.PopToRoot(slot);
    session.Page = 0;
    Activate(slot, session.Tabs[static_cast<std::size_t>(tab)].RootIndex);
}

void PanoramaMenu::TurnPage(int slot, int delta)
{
    const Menu* menu = _stack.Current(slot);
    if (!menu)
        return;

    _stack.ApplyPending(slot);
    Session& session = _sessions[slot];
    session.Page = WrapIndex(session.Page + delta, PageCount(static_cast<int>(menu->Items.size()), _layout.RowCount()));
    Draw(slot);
}

void PanoramaMenu::Hide(int slot)
{
    _sessions[slot] = {};
    _services.ChatInput.CancelCapture(slot);
    _services.Freeze.Close(slot);
    _layout.Hide(slot);
}

}  // namespace VoltMod
