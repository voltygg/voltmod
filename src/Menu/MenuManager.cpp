#include "Menu/CenterHtmlDriver.hpp"
#include "Menu/MenuKeys.hpp"
#include "Menu/PanoramaDriver.hpp"
#include "Menu/PendingCommit.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <algorithm>
#include <format>
#include <initializer_list>
#include <memory>
#include <utility>

namespace VoltMod
{

/** What a breadcrumb puts between two titles. */
static constexpr std::string_view kCrumbSeparator = " › ";

MenuManager::MenuManager(const MenuServices& services)
    : _services(services),
      _pending(std::make_unique<PendingCommit>(
          [&scheduler = services.Scheduler](int64_t delayMs, std::function<void()> callback) {
              return scheduler.Delay(delayMs, std::move(callback));
          })),
      _keys(std::make_unique<MenuKeys>(*this, _services)),
      _driver(std::make_unique<CenterHtmlDriver>(*this, _services))
{
    _states.BindReset(services.Slots);
    _pending->BindReset(services.Slots);
}

MenuManager::~MenuManager() = default;

Status MenuManager::UsePanorama(std::string_view layout)
{
    // Both capabilities first, then the name: nothing changes until every check has passed, so a
    // plugin that logs the error and carries on is still on a working center-HTML session.
    for (Capability needed : {Capability::CustomUi, Capability::UiClicks})
    {
        if (!_services.Capabilities.Has(needed))
        {
            return std::unexpected(
                Error::Unsupported(std::format("{} is off: {}", Name(needed), _services.Capabilities.Reason(needed))));
        }
    }

    auto panel = _services.Ui.Panel(layout);
    if (!panel)
        return std::unexpected(panel.error());

    CloseAllSessions();
    _layout = std::string(layout);
    _driver = std::make_unique<PanoramaDriver>(*this, _services, std::move(*panel));
    Log::Info("Menus draw into the '{}' Panorama layout.", _layout);
    return {};
}

void MenuManager::UseCenterHtml()
{
    if (!IsPanorama())
        return;

    CloseAllSessions();
    _layout.clear();
    _driver = std::make_unique<CenterHtmlDriver>(*this, _services);
    Log::Info("Menus draw as center HTML.");
}

bool MenuManager::IsPanorama() const noexcept
{
    // A Panorama layout name is never empty - ResolveLayoutName refuses one - so the name is also
    // the answer to which driver is drawing.
    return !_layout.empty();
}

std::string_view MenuManager::Layout() const noexcept
{
    return _layout;
}

void MenuManager::Open(int slot, std::shared_ptr<Menu> menu, MenuOptions options)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    auto& state = _states[slot];
    // Options belong to the call that opens the stack; a submenu pushed onto a live session
    // inherits it, so an unfrozen session stays unfrozen throughout.
    if (state.MenuStack.empty())
    {
        state.Keyboard = options.Keyboard;
        if (options.FreezeMovement)
            SetPlayerFrozen(slot, true);
    }

    state.MenuStack.push_back(std::move(menu));
    ResetCursor(slot);
    _driver->Reset(slot);

    if (auto* current = state.GetCurrentMenu())
        Log::Info("Menu opened for slot {} (title: {}, items: {})", slot, current->Title, current->Items.size());

    if (!_onFrame)
        _onFrame = _services.Scheduler.EveryFrame([this] { OnGameFrame(); });
}

void MenuManager::Open(int slot, std::shared_ptr<Menu> menu)
{
    Open(slot, std::move(menu), {});
}

void MenuManager::Close(int slot)
{
    if (!IsValidSlot(slot))
        return;

    // Before the stack moves: a value stepped and left showing is applied, not dropped.
    _pending->Run(slot);

    auto& state = _states[slot];
    if (state.MenuStack.empty())
        return;

    state.MenuStack.pop_back();
    Log::Info("Menu closed for slot {} ({} left on the stack)", slot, state.MenuStack.size());

    if (state.MenuStack.empty())
    {
        SetPlayerFrozen(slot, false);
        state.Reset();
        _driver->Dismiss(slot);
        return;
    }

    ResetCursor(slot);
    _driver->Reset(slot);
}

void MenuManager::CloseAll(int slot)
{
    if (!IsValidSlot(slot))
        return;

    _pending->Run(slot);
    SetPlayerFrozen(slot, false);
    _states[slot].Reset();
    Log::Info("All menus closed for slot {}", slot);
    _driver->Dismiss(slot);
}

void MenuManager::CloseAll(int slot, std::string_view replyKey)
{
    // Translate before closing: the reply is addressed to a player whose menus are about to go.
    if (auto& reply = _services.Policy.Reply; reply)
        reply(slot, _services.Translations.Get(std::string(replyKey), slot));

    CloseAll(slot);
}

void MenuManager::Prompt(int slot, std::string prompt, std::function<bool(int, std::string_view)> callback)
{
    _services.ChatInput.BeginCapture(slot, std::move(prompt), std::move(callback));
}

bool MenuManager::IsOpen(int slot) const
{
    return IsValidSlot(slot) && _states[slot].HasMenu();
}

void MenuManager::FreezeWhileOpen(bool enabled)
{
    _freezePlayer = enabled;

    if (enabled)
        return;

    // Turning it off releases whoever the previous setting already froze; leaving them stuck
    // until they close a menu they may not know is open is not a defensible reading of "off".
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_states[slot].MovementFrozen)
            SetPlayerFrozen(slot, false);
    }
}

bool MenuManager::IsCursorTarget(const MenuItem& item, int slot)
{
    if (!item.Describe)
        return false;
    const MenuRow row = item.Describe(slot);
    return row.Enabled && row.Selectable;
}

void MenuManager::StepCursor(int slot, const std::vector<MenuItem>& items, int& idx, int step)
{
    int n = static_cast<int>(items.size());
    if (n == 0)
        return;

    int attempts = n;
    do
    {
        idx = ((idx + step) % n + n) % n;
    }
    while (!IsCursorTarget(items[idx], slot) && --attempts > 0);
}

void MenuManager::SelectFirst(int slot, int& index)
{
    auto* menu = Current(slot);
    index = 0;
    if (menu && !menu->Items.empty() && !IsCursorTarget(menu->Items[0], slot))
        StepCursor(slot, menu->Items, index, +1);
}

void MenuManager::ResetCursor(int slot)
{
    if (!IsValidSlot(slot))
        return;

    auto& state = _states[slot];
    state.LastInputTime = Time::MonotonicMs();
    state.Rows.clear();
    SelectFirst(slot, state.SelectedIndex);
}

int MenuManager::Selected(int slot) const
{
    return IsValidSlot(slot) ? _states[slot].SelectedIndex : 0;
}

void MenuManager::Select(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    // Leaving a stepped row applies what it was left showing. Landing back on the row that is
    // still waiting leaves it waiting, so W-then-S over one row is not an action.
    if (!_pending->IsPending(slot, index))
        _pending->Run(slot);

    _states[slot].SelectedIndex = index;
}

void MenuManager::SelectOnPage(int slot, int page, int rowsPerPage)
{
    auto* menu = Current(slot);
    if (!menu || menu->Items.empty() || rowsPerPage <= 0)
        return;

    const int items = static_cast<int>(menu->Items.size());
    const int start = std::clamp(page * rowsPerPage, 0, items - 1);
    const int end = std::min(items, start + rowsPerPage);

    int index = start;
    while (index < end && !IsCursorTarget(menu->Items[static_cast<std::size_t>(index)], slot))
        ++index;

    Select(slot, index < end ? index : start);
}

std::string MenuManager::Crumbs(int slot) const
{
    if (!IsValidSlot(slot))
        return {};

    // Everything under the top menu, which is the path taken to reach what is on screen; the
    // current title is drawn on its own and would only repeat itself here.
    const auto& stack = _states[slot].MenuStack;
    std::string crumbs;
    for (std::size_t i = 0; i + 1 < stack.size(); ++i)
    {
        if (!crumbs.empty())
            crumbs += kCrumbSeparator;
        crumbs += stack[i]->Title;
    }
    return crumbs;
}

bool MenuManager::KeyboardEnabled(int slot) const
{
    return IsValidSlot(slot) && _states[slot].Keyboard;
}

bool MenuManager::HandleKeys(int slot, MenuDriver& driver)
{
    return _keys->Handle(slot, driver);
}

MenuRow MenuManager::Describe(int slot, int index)
{
    auto* menu = Current(slot);
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return MenuRow{.Enabled = false, .Selectable = false};

    // An item with no Describe is malformed; it draws as an inert line rather than a row the
    // cursor could land on.
    const MenuItem& item = menu->Items[static_cast<std::size_t>(index)];
    MenuRow row = item.Describe ? item.Describe(slot) : MenuRow{.Enabled = false, .Selectable = false};

    auto& state = _states[slot];
    if (state.Rows.size() != menu->Items.size())
        state.Rows.assign(menu->Items.size(), MenuRowMemory{});

    const int64_t now = Time::MonotonicMs();
    MenuRowMemory& memory = state.Rows[static_cast<std::size_t>(index)];
    if (memory.Value != row.Value)
    {
        // Arriving on screen is not a change: only a value that moves under a row already drawn
        // is worth flashing.
        if (memory.Drawn)
            memory.ChangedAt = now;
        memory.Value = row.Value;
        memory.Drawn = true;
    }

    row.Changed = memory.ChangedAt != 0 && now - memory.ChangedAt < ChangedMs;
    row.Pending = _pending->IsPending(slot, index);
    return row;
}

void MenuManager::Activate(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    // A row whose activation *is* its commit - a ChoiceRow's E - would apply the value twice if
    // the held one ran as well, so pressing the pending row cancels the wait and lets the
    // activation apply it. Any other row runs what is held first, then does its own thing.
    if (_pending->IsPending(slot, index))
        _pending->Cancel(slot);
    else
        _pending->Run(slot);

    // Re-read: running a commit may have closed or replaced the menu.
    auto* menu = Current(slot);
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return;

    // Copies out of the vector, not references into it: a row that closes or reopens the menu
    // destroys the Menu, and with it the item whose handler is still running.
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (item.Activate && IsCursorTarget(item, slot))
        item.Activate(slot, *this);
}

bool MenuManager::Step(int slot, int index, int direction)
{
    auto* menu = Current(slot);
    if (!IsValidSlot(slot) || !menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return false;

    // Copied for the same reason as in Activate: a step that persists may rebuild the menu.
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (!item.Step || !item.Describe || !item.Describe(slot).Enabled)
        return false;
    if (!item.Step(slot, direction))
        return false;

    // The row now shows a value nothing has applied. Holding the commit is what turns a burst of
    // presses into one action; the row draws as pending until it runs.
    if (item.Commit)
        _pending->Arm(slot, index, [commit = item.Commit, slot] { commit(slot); });

    return true;
}

Menu* MenuManager::Current(int slot)
{
    return IsValidSlot(slot) ? _states[slot].GetCurrentMenu() : nullptr;
}

int MenuManager::Depth(int slot) const
{
    return IsValidSlot(slot) ? static_cast<int>(_states[slot].MenuStack.size()) : 0;
}

void MenuManager::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (!_states[slot].HasMenu())
            continue;

        _driver->HandleInput(slot);
        // Input may have activated a row that closed the menu it was about to draw.
        if (_states[slot].HasMenu())
            _driver->Present(slot);
    }

    // A slot changing hands empties a stack without going through Close, so this - rather than
    // each close path - is what stops the per-frame cost once nothing is open.
    if (!AnyOpen())
        _onFrame.Reset();
}

bool MenuManager::AnyOpen() const
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_states[slot].HasMenu())
            return true;
    }
    return false;
}

void MenuManager::CloseAllSessions()
{
    // Before any session goes: a driver swap is not a reason to drop a value a player picked.
    _pending->RunAll();

    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_states[slot].HasMenu())
            CloseAll(slot);
    }
}

void MenuManager::SetPlayerFrozen(int slot, bool frozen)
{
    // Only the freeze direction is gated. Releasing must always run: gating both meant turning
    // the setting off while sessions were open stranded whoever was already frozen, with no
    // path back short of a reconnect.
    if (frozen && !_freezePlayer)
        return;

    auto& state = _states[slot];

    // Skip redundant transitions so a freeze isn't double-applied (which would capture
    // MOVETYPE_NONE as the "previous" type) and an unfreeze isn't run on a never-frozen slot.
    if (frozen == state.MovementFrozen)
        return;

    Pawn pawn = _services.Entities.PawnOf(slot);
    if (!pawn)
        return;

    if (frozen)
        state.PrevMoveType = pawn.Move();

    pawn.SetMove(frozen ? MoveType::None : state.PrevMoveType);
    state.MovementFrozen = frozen;
}

}  // namespace VoltMod
