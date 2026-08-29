#include "Menu/CenterHtmlDriver.hpp"
#include "Menu/MenuCursor.hpp"
#include "Menu/MenuKeys.hpp"
#include "Menu/PanoramaDriver.hpp"
#include "Menu/PendingCommit.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <format>
#include <initializer_list>
#include <memory>
#include <utility>

// The session half of the manager: which driver is drawing, what is on each player's stack, the
// per-frame loop over open sessions, and the movement freeze. What a driver asks about a row -
// and what a press does to it - is in MenuManagerRows.cpp.

namespace VoltMod
{

MenuManager::MenuManager(const MenuServices& services)
    : _services(services),
      _pending(std::make_unique<PendingCommit>(
          [&scheduler = services.Scheduler](int64_t delayMs, std::function<void()> callback) {
              return scheduler.Delay(delayMs, std::move(callback));
          })),
      _cursor(std::make_unique<MenuCursor>()),
      _keys(std::make_unique<MenuKeys>(*this, _services)),
      _driver(std::make_unique<CenterHtmlDriver>(*this, _services))
{
    _states.BindReset(services.Slots);
    _cursor->BindReset(services.Slots);
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
    _fallback = std::make_unique<CenterHtmlDriver>(*this, _services);
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
    _fallback.reset();
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

    if (_states[slot].HasMenu())
        CloseAll(slot);

    auto& state = _states[slot];
    state.Keyboard = options.Keyboard;
    state.FreezeMovement = options.FreezeMovement;
    if (options.FreezeMovement)
        SetPlayerFrozen(slot, true, _services.Entities.PawnOf(slot));

    Push(slot, std::move(menu));
}

void MenuManager::Open(int slot, std::shared_ptr<Menu> menu)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    if (!_states[slot].HasMenu())
    {
        Open(slot, std::move(menu), {});
        return;
    }

    Push(slot, std::move(menu));
}

void MenuManager::Push(int slot, std::shared_ptr<Menu> menu)
{
    auto& state = _states[slot];
    state.MenuStack.push_back(std::move(menu));
    ResetCursor(slot);
    // No SyncDriver here: OnGameFrame picks the driver before the first Present, and resetting
    // twice on a frame that also flips the driver is what this leaves out.
    DriverOf(slot).Reset(slot);

    if (auto* current = state.GetCurrentMenu())
        Log::Info("Menu opened for slot {} (title: {}, depth: {}, items: {})", slot, current->Title,
                  state.MenuStack.size(), current->Items.size());

    if (!_onFrame)
        _onFrame = _services.Scheduler.EveryFrame([this] { OnGameFrame(); });
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
        MenuDriver& driver = DriverOf(slot);
        SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
        state.Reset();
        _cursor->Select(slot, 0);
        driver.Dismiss(slot);
        return;
    }

    ResetCursor(slot);
    DriverOf(slot).Reset(slot);
}

void MenuManager::CloseAll(int slot)
{
    if (!IsValidSlot(slot))
        return;

    _pending->Run(slot);
    MenuDriver& driver = DriverOf(slot);
    SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    _states[slot].Reset();
    _cursor->Select(slot, 0);
    Log::Info("All menus closed for slot {}", slot);
    driver.Dismiss(slot);
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
        if (_states[slot].FrozenPawn)
            SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    }
}

void MenuManager::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (!_states[slot].HasMenu())
            continue;

        // One resolve for the frame: the freeze and the driver choice ask about the same body.
        const Pawn pawn = _services.Entities.PawnOf(slot);
        SyncFreeze(slot, pawn);
        SyncDriver(slot, pawn);
        DriverOf(slot).HandleInput(slot);
        // Input may have activated a row that closed the menu it was about to draw.
        if (_states[slot].HasMenu())
            DriverOf(slot).Present(slot);
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

void MenuManager::SetPlayerFrozen(int slot, bool frozen, const Pawn& pawn)
{
    // Only the freeze direction is gated. Releasing must always run: gating both meant turning
    // the setting off while sessions were open stranded whoever was already frozen, with no
    // path back short of a reconnect.
    if (frozen && !_freezePlayer)
        return;

    auto& state = _states[slot];

    // Skip redundant transitions so a freeze isn't double-applied (which would capture
    // MOVETYPE_NONE as the "previous" type) and an unfreeze isn't run on a never-frozen slot.
    if (frozen == static_cast<bool>(state.FrozenPawn))
        return;

    if (frozen)
    {
        if (!pawn || !pawn.IsAlive())
            return;

        state.PrevMoveType = pawn.Move();
        state.FrozenPawn = pawn.Ref();
        pawn.SetMove(MoveType::None);
        return;
    }

    if (pawn && pawn.Ref() == state.FrozenPawn)
        pawn.SetMove(state.PrevMoveType);
    state.FrozenPawn = {};
}

void MenuManager::SyncFreeze(int slot, const Pawn& pawn)
{
    auto& state = _states[slot];
    if (!_freezePlayer || !state.FreezeMovement)
        return;

    // A pawn that died or was replaced is let go without a write: its move type must not reach
    // the next body.
    if (state.FrozenPawn && (!pawn || !pawn.IsAlive() || pawn.Ref() != state.FrozenPawn))
        state.FrozenPawn = {};

    SetPlayerFrozen(slot, true, pawn);
}

MenuDriver& MenuManager::DriverOf(int slot)
{
    return _states[slot].OnFallback && _fallback ? *_fallback : *_driver;
}

void MenuManager::SyncDriver(int slot, const Pawn& pawn)
{
    auto& state = _states[slot];
    const bool fallback = IsPanorama() && !(pawn && pawn.IsAlive());
    if (fallback == state.OnFallback)
        return;

    DriverOf(slot).Dismiss(slot);
    state.OnFallback = fallback;
    DriverOf(slot).Reset(slot);
    Log::Info("Menu for slot {} drawn as {}.", slot, fallback ? "center HTML (not alive)" : "Panorama");
}

}  // namespace VoltMod
