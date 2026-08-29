#include "Menu/CenterHtmlDriver.hpp"
#include "Menu/PanoramaDriver.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <format>
#include <initializer_list>
#include <memory>
#include <utility>

namespace VoltMod
{

MenuManager::MenuManager(const MenuServices& services)
    : _services(services), _driver(std::make_unique<CenterHtmlDriver>(*this, _services))
{
    _states.BindReset(services.Slots);
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
            return std::unexpected(Error::Unsupported(
                std::format("{} is off: {}", Name(needed), _services.Capabilities.Reason(needed))));
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
    if (state.MenuStack.empty() && options.FreezeMovement)
        SetPlayerFrozen(slot, true);

    state.MenuStack.push(std::move(menu));
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

    auto& state = _states[slot];
    if (state.MenuStack.empty())
        return;

    state.MenuStack.pop();
    Log::Info("Menu closed for slot {} ({} left on the stack)", slot, state.MenuStack.size());

    if (state.MenuStack.empty())
    {
        SetPlayerFrozen(slot, false);
        state.Reset();
        _driver->Dismiss(slot);
        return;
    }

    _driver->Reset(slot);
}

void MenuManager::CloseAll(int slot)
{
    if (!IsValidSlot(slot))
        return;

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

void MenuManager::Activate(int slot, int index)
{
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
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return false;

    // Copied for the same reason as in Activate: a step that persists may rebuild the menu.
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (!item.Step || !item.Describe || !item.Describe(slot).Enabled)
        return false;
    return item.Step(slot, direction);
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
