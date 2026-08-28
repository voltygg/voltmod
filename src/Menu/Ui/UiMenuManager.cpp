#include <VoltMod/Menu/MenuOption.hpp>
#include <VoltMod/Menu/Ui/UiMenuManager.hpp>
#include <algorithm>
#include <format>
#include <utility>

namespace VoltMod
{

UiMenuManager::UiMenuManager(Scheduler& scheduler, CustomUi& ui, SlotEvents& slots, EntitySystem& entities,
                             ChatInput& chatInput, Translations& translations, Policy& policy, PlayerManager& players,
                             std::string layout)
    : MenuHost(slots, entities, chatInput, translations, policy, players),
      _layout(ui, slots, std::move(layout)),
      _rows(_layout, RootId, RowPrefix, RowsPerPage),
      _pump(scheduler.EveryFrame([this] { OnGameFrame(); }))
{
    Bind(slots);
}

std::string_view UiMenuManager::ModifierFor(MenuRowKind kind)
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

void UiMenuManager::SetLayout(std::string layout)
{
    if (layout == _layout.Name())
        return;

    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_states[slot].HasMenu())
            CloseAllMenus(slot);
    }

    _layout.Retarget(std::move(layout));
}

void UiMenuManager::Bind(SlotEvents& slots)
{
    _subs.push_back(_rows.Pressed += [this](int slot, int row) { OnRowPressed(slot, row); });
    _subs.push_back(_rows.Stepped += [this](int slot, int row, int dir) { OnRowStepped(slot, row, dir); });

    _subs.push_back(slots.Changed += [this](int slot) {
        if (IsValidSlot(slot))
            _shown[static_cast<std::size_t>(slot)] = true;
    });
}

void UiMenuManager::BindNav()
{
    if (!_nav.empty())
        return;

    _nav.push_back(_layout.OnClick(std::string(BackId), [this](int slot) { CloseMenu(slot); }));
    _nav.push_back(_layout.OnClick(std::string(CloseId), [this](int slot) { CloseAllMenus(slot); }));
    _nav.push_back(_layout.OnClick(std::string(PrevId), [this](int slot) { TurnPage(slot, -1); }));
    _nav.push_back(_layout.OnClick(std::string(NextId), [this](int slot) { TurnPage(slot, +1); }));
    _nav.push_back(_layout.OnClick(std::string(CancelId), [this](int slot) { _chatInput.CancelCapture(slot); }));
}

int UiMenuManager::ItemIndex(int slot, int row) const
{
    return _states[slot].Page * RowsPerPage + row;
}

void UiMenuManager::OnRowPressed(int slot, int row)
{
    // Rows are inert behind the chat prompt; the press to honour there is Cancel.
    if (!IsValidSlot(slot) || _chatInput.IsCapturing(slot))
        return;

    Activate(slot, ItemIndex(slot, row));
}

void UiMenuManager::OnRowStepped(int slot, int row, int direction)
{
    if (!IsValidSlot(slot) || _chatInput.IsCapturing(slot))
        return;

    Step(slot, ItemIndex(slot, row), direction);
}

void UiMenuManager::TurnPage(int slot, int delta)
{
    if (!IsValidSlot(slot) || _chatInput.IsCapturing(slot))
        return;

    auto& state = _states[slot];
    auto* menu = state.GetCurrentMenu();
    if (!menu)
        return;

    const int pages = PageCount(static_cast<int>(menu->Items.size()), RowsPerPage);
    state.Page = ((state.Page + delta) % pages + pages) % pages;
}

void UiMenuManager::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_states[slot].HasMenu())
            Present(slot);
        else if (_shown[static_cast<std::size_t>(slot)])
            Hide(slot);
    }
}

void UiMenuManager::Present(int slot)
{
    auto& state = _states[slot];
    auto* menu = state.GetCurrentMenu();
    if (!menu)
        return;

    if (!_layout.EnsureFor(slot))
        return;

    BindNav();

    const int items = static_cast<int>(menu->Items.size());
    const int pages = PageCount(items, RowsPerPage);
    state.Page = std::clamp(state.Page, 0, pages - 1);

    _layout.Text(slot, RootId, TitleVar, menu->Title);
    _layout.Text(slot, RootId, SubtitleVar, menu->Subtitle);
    _layout.Class(slot, SubtitleId, "Hidden", menu->Subtitle.empty());

    const auto prompt = _chatInput.GetPrompt(slot);
    _layout.Class(slot, PromptId, "Hidden", !prompt.has_value());
    if (prompt)
        _layout.Text(slot, RootId, PromptVar, *prompt);

    const int first = state.Page * RowsPerPage;
    const int last = std::min(items, first + RowsPerPage);

    for (int index = first; index < last; ++index)
    {
        const auto& option = menu->Items[static_cast<std::size_t>(index)];
        if (!option)
            continue;

        const MenuRow row = option->Describe(slot);
        _rows.Set(slot, index - first,
                  UiRow{.Label = row.Label,
                        .Value = row.Value,
                        .Modifier = ModifierFor(row.Kind),
                        .Enabled = option->IsEnabled(),
                        // The same question the HTML footer asks, so both drivers read one answer.
                        .Steppers = option->UsesHorizontal()});
    }
    _rows.HideFrom(slot, last - first);

    _layout.Class(slot, PagerId, "Hidden", pages <= 1);
    if (pages > 1)
        _layout.Text(slot, RootId, PageVar, std::format("{}/{}", state.Page + 1, pages));

    _layout.Class(slot, BackId, "Hidden", state.MenuStack.size() <= 1);

    _layout.Class(slot, RootId, "Hidden", false);
    _layout.InputCapture(slot, true);
    _shown[static_cast<std::size_t>(slot)] = true;
}

void UiMenuManager::Dismiss(int)
{
    // The pump hides the slot next frame.
}

void UiMenuManager::Hide(int slot)
{
    _shown[static_cast<std::size_t>(slot)] = false;

    if (!_layout.Covers(slot))
        return;

    _layout.Class(slot, RootId, "Hidden", true);
    _layout.InputCapture(slot, false);

    _layout.Forget(slot);
}

}  // namespace VoltMod
