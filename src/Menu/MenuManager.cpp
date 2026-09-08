#include "Menu/ActiveMenus.hpp"
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
    : _services(services),
      _menus(std::make_unique<ActiveMenus>(
          *this, _services.Translations,
          [&scheduler = services.Scheduler](int64_t delayMs, std::function<void()> callback) {
              return scheduler.Delay(delayMs, std::move(callback));
          })),
      _driver(std::make_unique<CenterHtmlDriver>(*_menus, *this, _services))
{
    _menus->BindReset(services.Slots);
}

MenuManager::~MenuManager() = default;

Status MenuManager::UsePanorama(std::string_view layout)
{
    // Transmit is what keeps each player's panel theirs alone.
    for (Capability needed : {Capability::CustomUi, Capability::UiClicks, Capability::Transmit})
    {
        if (!_services.Capabilities.Has(needed))
        {
            return std::unexpected(
                Error::Unsupported(std::format("{} is off: {}", Name(needed), _services.Capabilities.Reason(needed))));
        }
    }

    // Refuse a bad layout name once here rather than once per player.
    if (auto panel = _services.Ui.Panel(layout); !panel)
        return std::unexpected(panel.error());

    CloseAllSessions();
    _layout = std::string(layout);
    _driver = std::make_unique<PanoramaDriver>(*_menus, *this, _services, _layout);
    _fallback = std::make_unique<CenterHtmlDriver>(*_menus, *this, _services);
    Log::Info("Menus draw into the '{}' Panorama layout.", _layout);
    return {};
}

void MenuManager::UseCenterHtml()
{
    if (!IsPanorama())
        return;

    CloseAllSessions();
    _layout.clear();
    _driver = std::make_unique<CenterHtmlDriver>(*_menus, *this, _services);
    _fallback.reset();
    Log::Info("Menus draw as center HTML.");
}

void MenuManager::Open(int slot, std::shared_ptr<Menu> menu, MenuOptions options)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    if (_menus->IsOpen(slot))
        CloseAll(slot);

    auto& state = _menus->State(slot);
    state.Keyboard = options.Keyboard;
    state.FreezeMovement = options.FreezeMovement;
    // A new session gets Panorama another try.
    state.OnFallback = false;
    if (options.FreezeMovement)
        SetPlayerFrozen(slot, true, _services.Entities.PawnOf(slot));

    Push(slot, std::move(menu));
}

void MenuManager::Open(int slot, std::shared_ptr<Menu> menu)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    if (!_menus->IsOpen(slot))
    {
        Open(slot, std::move(menu), {});
        return;
    }

    Push(slot, std::move(menu));
}

void MenuManager::Push(int slot, std::shared_ptr<Menu> menu)
{
    _menus->Push(slot, std::move(menu));
    // OnGameFrame selects the driver before Present; avoid resetting twice during a swap.
    DriverOf(slot).Reset(slot);

    if (auto* current = _menus->Current(slot))
    {
        Log::Info("Menu opened for slot {} (title: {}, depth: {}, items: {})", slot, current->Title,
                  _menus->Depth(slot), current->Items.size());
    }

    if (!_onFrame)
        _onFrame = _services.Scheduler.EveryFrame([this] { OnGameFrame(); });
}

void MenuManager::Close(int slot)
{
    if (!IsValidSlot(slot))
        return;

    // Clear prompts for menus that are closing so they cannot consume later chat input.
    _services.ChatInput.CancelCapture(slot);

    if (!_menus->IsOpen(slot))
        return;

    MenuDriver& driver = DriverOf(slot);
    if (!_menus->Pop(slot))
    {
        Log::Info("Menu closed for slot {} ({} left on the stack)", slot, _menus->Depth(slot));
        driver.Reset(slot);
        return;
    }

    Log::Info("Menu closed for slot {} (0 left on the stack)", slot);
    SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    driver.Dismiss(slot);
}

void MenuManager::CloseAll(int slot)
{
    if (!IsValidSlot(slot))
        return;

    _services.ChatInput.CancelCapture(slot);

    MenuDriver& driver = DriverOf(slot);
    _menus->Clear(slot);
    SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
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

std::string MenuManager::Translate(int slot, std::string_view key, std::string_view fallback) const
{
    return _services.Translations.GetOr(key, slot, fallback);
}

bool MenuManager::IsOpen(int slot) const
{
    return _menus->IsOpen(slot);
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
        if (_menus->State(slot).FrozenPawn)
            SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    }
}

void MenuManager::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (!_menus->IsOpen(slot))
            continue;

        SyncFreeze(slot, _services.Entities.PawnOf(slot));
        DriverOf(slot).HandleInput(slot);
        // Input may have activated a row that closed the menu it was about to draw.
        if (_menus->IsOpen(slot) && !DriverOf(slot).Present(slot))
            FallBack(slot);
    }

    // Slot reset clears stacks without going through Close, so stop per-frame work here.
    if (!_menus->AnyOpen())
        _onFrame.Reset();
}

void MenuManager::CloseAllSessions()
{
    // Preserve values selected before a driver swap.
    _menus->RunPending();

    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_menus->IsOpen(slot))
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

    auto& state = _menus->State(slot);

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
    auto& state = _menus->State(slot);
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
    return _menus->State(slot).OnFallback && _fallback ? *_fallback : *_driver;
}

void MenuManager::FallBack(int slot)
{
    auto& state = _menus->State(slot);
    if (state.OnFallback || !_fallback)
        return;

    _driver->Dismiss(slot);
    state.OnFallback = true;
    _fallback->Reset(slot);
    (void)_fallback->Present(slot);
    Log::Info("Menu for slot {} drawn as center HTML: its Panorama panel could not be shown.", slot);
}

}  // namespace VoltMod
