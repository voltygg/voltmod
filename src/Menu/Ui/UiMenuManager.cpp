#include "Menu/Ui/UiList.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Menu/Ui/UiMenuManager.hpp>
#include <algorithm>
#include <format>
#include <memory>
#include <utility>

namespace VoltMod
{

UiMenuManager::UiMenuManager(Scheduler& scheduler, CustomUi& ui, SlotEvents& slots, EntitySystem& entities,
                             ChatInput& chatInput, Translations& translations, Policy& policy, std::string layout)
    : MenuHost(slots, entities, chatInput, translations, policy),
      _rows(std::make_unique<UiList>(_panel, RootId, RowPrefix, RowsPerPage)),
      _onFrame(scheduler.EveryFrame([this] { OnGameFrame(); }))
{
    // Nothing is spawned here: the panel holds the name and the first draw asks for the entity.
    if (auto panel = ui.Panel(layout))
        _panel = std::move(*panel);
    else
        Log::Warn("UiMenuManager: '{}' is not a usable layout name ({}).", layout, panel.error().Detail);

    Bind(slots);
}

UiMenuManager::~UiMenuManager() = default;

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
    if (layout == _panel.Name())
        return;

    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_states[slot].HasMenu())
            CloseAllMenus(slot);
    }

    _panel.SetLayout(std::move(layout));
}

void UiMenuManager::Bind(SlotEvents& slots)
{
    _subs.On(_rows->Pressed, [this](int slot, int row) { OnRowPressed(slot, row); });
    _subs.On(_rows->Stepped, [this](int slot, int row, int dir) { OnRowStepped(slot, row, dir); });

    _subs.On(slots.Changed, [this](int slot) {
        if (IsValidSlot(slot))
            _shown[static_cast<std::size_t>(slot)] = true;
    });
}

void UiMenuManager::BindNav()
{
    if (!_nav.Empty())
        return;

    _nav.On(_panel.Button(BackId), [this](int slot) { CloseMenu(slot); });
    _nav.On(_panel.Button(CloseId), [this](int slot) { CloseAllMenus(slot); });
    _nav.On(_panel.Button(PrevId), [this](int slot) { TurnPage(slot, -1); });
    _nav.On(_panel.Button(NextId), [this](int slot) { TurnPage(slot, +1); });
    _nav.On(_panel.Button(CancelId), [this](int slot) { _chatInput.CancelCapture(slot); });
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

    if (!_panel.Ensure(slot))
        return;

    BindNav();

    const int items = static_cast<int>(menu->Items.size());
    const int pages = PageCount(items, RowsPerPage);
    state.Page = std::clamp(state.Page, 0, pages - 1);

    // Every write below discards its Status: the panel logs the first failure for a slot itself,
    // and the next frame redraws anyway.
    (void)_panel.Text(slot, RootId, TitleVar, menu->Title);
    (void)_panel.Text(slot, RootId, SubtitleVar, menu->Subtitle);
    (void)_panel.Class(slot, SubtitleId, "Hidden", menu->Subtitle.empty());

    const auto prompt = _chatInput.GetPrompt(slot);
    (void)_panel.Class(slot, PromptId, "Hidden", !prompt.has_value());
    if (prompt)
        (void)_panel.Text(slot, RootId, PromptVar, *prompt);

    const int first = state.Page * RowsPerPage;
    const int last = std::min(items, first + RowsPerPage);

    for (int index = first; index < last; ++index)
    {
        const MenuItem& item = menu->Items[static_cast<std::size_t>(index)];
        if (!item.Describe)
            continue;

        const MenuRow row = item.Describe(slot);
        _rows->Set(slot, index - first,
                   UiRow{.Label = row.Label,
                         .Value = row.Value,
                         .Modifier = ModifierFor(row.Kind),
                         .Enabled = row.Enabled,
                         // The same question the HTML footer asks, so both drivers read one answer.
                         .Steppers = row.Steppable});
    }
    _rows->HideFrom(slot, last - first);

    (void)_panel.Class(slot, PagerId, "Hidden", pages <= 1);
    if (pages > 1)
        (void)_panel.Text(slot, RootId, PageVar, std::format("{}/{}", state.Page + 1, pages));

    (void)_panel.Class(slot, BackId, "Hidden", state.MenuStack.size() <= 1);

    (void)_panel.Class(slot, RootId, "Hidden", false);
    (void)_panel.InputCapture(slot, true);
    _shown[static_cast<std::size_t>(slot)] = true;
}

void UiMenuManager::Dismiss(int)
{
    // The per-frame redraw hides the slot next frame.
}

void UiMenuManager::Hide(int slot)
{
    _shown[static_cast<std::size_t>(slot)] = false;

    if (!_panel.Covers(slot))
        return;

    (void)_panel.Class(slot, RootId, "Hidden", true);
    (void)_panel.InputCapture(slot, false);

    _panel.Forget(slot);
}

}  // namespace VoltMod
