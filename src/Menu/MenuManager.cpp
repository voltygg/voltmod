#include "Menu/ActiveMenus.hpp"
#include "Menu/CenterHtmlRender.hpp"
#include "Menu/MenuCursor.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <memory>
#include <utility>

namespace VoltMod
{

MenuManager::MenuManager(const MenuServices& services)
    : _services(services),
      _menus(std::make_unique<ActiveMenus>(
          *this, _services.Translations, [&scheduler = services.Scheduler](int64_t delayMs, std::function<void()> callback) {
              return scheduler.Delay(delayMs, std::move(callback));
          }))
{
    _menus->BindReset(services.Slots);
}

MenuManager::~MenuManager() = default;

void MenuManager::Open(int slot, std::shared_ptr<Menu> menu, MenuOptions options)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    CloseAll(slot);

    _menus->State(slot).FreezeMovement = options.FreezeMovement;
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

    if (auto* current = _menus->Current(slot))
    {
        Log::Info("Menu opened for slot {} (title: {}, depth: {}, items: {})", slot, current->Title, _menus->Depth(slot),
                  current->Items.size());
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

    // A parent menu is showing again; the next frame draws it.
    if (!_menus->Pop(slot))
    {
        Log::Info("Menu closed for slot {} ({} left on the stack)", slot, _menus->Depth(slot));
        return;
    }

    Log::Info("Menu closed for slot {} (0 left on the stack)", slot);
    SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    _services.Messages.ClearCenterHtml(slot);
}

void MenuManager::CloseAll(int slot)
{
    if (!IsValidSlot(slot))
        return;

    _services.ChatInput.CancelCapture(slot);

    if (!_menus->IsOpen(slot))
        return;

    _menus->Clear(slot);
    SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    Log::Info("All menus closed for slot {}", slot);
    _services.Messages.ClearCenterHtml(slot);
}

void MenuManager::CloseAll(int slot, std::string_view replyKey)
{
    // Reply before closing: it is addressed to a player whose menus are about to go.
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
        if (_menus->State(slot).Freeze)
            SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    }
}

void MenuManager::Present(int slot)
{
    Menu* menu = _menus->Current(slot);
    if (!menu)
        return;

    // A pending capture replaces the item list with its prompt.
    if (auto prompt = _services.ChatInput.GetPrompt(slot))
    {
        _services.Messages.SendCenterHtml(slot, RenderCaptureOverlay(menu->Title, *prompt));
        return;
    }

    const CenterHtmlView view{
        .Describe = [this, slot](int index) { return _menus->Describe(slot, index); },
        .Breadcrumb = _menus->Breadcrumb(slot),
        .Slot = slot,
        .SelectedIndex = _menus->Selected(slot),
        .IsSubmenu = _menus->Depth(slot) > 1,
    };
    _services.Messages.SendCenterHtml(slot, RenderMenuHtml(menu, view, _services.Translations));
}

void MenuManager::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (!_menus->IsOpen(slot))
            continue;

        SyncFreeze(slot, _services.Entities.PawnOf(slot));
        HandleKeys(slot);

        // Input may have activated a row that closed the menu it was about to draw.
        if (_menus->IsOpen(slot))
            Present(slot);
    }

    // Slot reset clears stacks without going through Close, so stop per-frame work here.
    if (!_menus->AnyOpen())
        _onFrame.Reset();
}

bool MenuManager::HandleKeys(int slot)
{
    if (!_menus->Current(slot))
        return false;

    PlayerMenuState& state = _menus->State(slot);
    const uint64_t buttons = _services.Entities.Buttons(slot);
    const uint64_t pressed = buttons & ~state.PrevButtons;
    state.PrevButtons = buttons;

    if (pressed == 0)
        return false;

    const int64_t now = Time::MonotonicMs();
    if (now - state.LastInputTime < InputDebounceMs)
        return false;

    if (_services.ChatInput.IsCapturing(slot))
    {
        if ((pressed & IN_RELOAD) == 0)
            return false;

        _services.ChatInput.CancelCapture(slot);
        state.LastInputTime = now;
        return true;
    }

    if (!HandlePressed(slot, pressed))
        return false;

    // The action may have replaced the session.
    _menus->State(slot).LastInputTime = now;
    return true;
}

bool MenuManager::HandlePressed(int slot, uint64_t pressed)
{
    if (pressed & IN_RELOAD)
    {
        Close(slot);
        return true;
    }

    Menu* menu = _menus->Current(slot);
    const int itemCount = menu ? static_cast<int>(menu->Items.size()) : 0;
    if (itemCount == 0)
        return false;

    if (pressed & IN_FORWARD)
    {
        MoveCursor(slot, -1);
        return true;
    }
    if (pressed & IN_BACK)
    {
        MoveCursor(slot, +1);
        return true;
    }
    if (pressed & (IN_MOVELEFT | IN_MOVERIGHT))
    {
        const int direction = (pressed & IN_MOVELEFT) ? -1 : +1;
        if (_menus->Step(slot, _menus->Selected(slot), direction))
            return true;
        if (itemCount <= ItemsPerPage)
            return false;
        JumpPage(slot, direction);
        return true;
    }
    if (pressed & IN_USE)
    {
        _menus->Activate(slot, _menus->Selected(slot));
        return true;
    }
    return false;
}

void MenuManager::MoveCursor(int slot, int step)
{
    if (!_menus->Current(slot))
        return;

    _menus->Select(slot, MenuCursor::Step(_menus->Rows(slot), _menus->Selected(slot), step));
}

void MenuManager::JumpPage(int slot, int delta)
{
    Menu* menu = _menus->Current(slot);
    if (!menu || menu->Items.empty())
        return;

    _menus->Select(slot, MenuCursor::JumpPage(_menus->Rows(slot), _menus->Selected(slot), ItemsPerPage, delta));
}

void MenuManager::SetPlayerFrozen(int slot, bool frozen, const Pawn& pawn)
{
    // Only the freeze direction is gated. Releasing must always run: gating both meant turning
    // the setting off while sessions were open stranded whoever was already frozen, with no
    // path back short of a reconnect.
    if (frozen && !_freezePlayer)
        return;

    MovementFreeze& freeze = _menus->State(slot).Freeze;
    if (frozen)
        freeze.Hold(pawn);
    else
        freeze.Release(pawn);
}

void MenuManager::SyncFreeze(int slot, const Pawn& pawn)
{
    auto& state = _menus->State(slot);
    if (!_freezePlayer || !state.FreezeMovement)
        return;

    state.Freeze.Sync(pawn);
}

}  // namespace VoltMod
