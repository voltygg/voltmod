#include "Menu/MenuCore.hpp"

#include "Menu/MenuCursor.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Time.hpp>
#include <utility>

namespace VoltMod
{

MenuCore::MenuCore(const MenuServices& services, MenuSession& host)
    : _services(services),
      _host(host),
      _menus(std::make_unique<ActiveMenus>(
          host, _services.Translations, [&scheduler = services.Scheduler](int64_t delayMs, std::function<void()> callback) {
              return scheduler.Delay(delayMs, std::move(callback));
          }))
{
    _menus->BindReset(services.Slots);
}

MenuCore::~MenuCore() = default;

void MenuCore::Start(int slot, std::shared_ptr<Menu> menu, MenuOptions options)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    // Through the host, so its drawing comes down with the session it belonged to.
    if (_menus->IsOpen(slot))
        _host.CloseAll(slot);

    auto& state = _menus->State(slot);
    state.Keyboard = options.Keyboard;
    state.FreezeMovement = options.FreezeMovement;
    if (options.FreezeMovement)
        SetPlayerFrozen(slot, true, _services.Entities.PawnOf(slot));

    Push(slot, std::move(menu));
}

void MenuCore::Push(int slot, std::shared_ptr<Menu> menu)
{
    _menus->Push(slot, std::move(menu));

    if (auto* current = _menus->Current(slot))
    {
        Log::Info("Menu opened for slot {} (title: {}, depth: {}, items: {})", slot, current->Title,
                  _menus->Depth(slot), current->Items.size());
    }

    if (!_onFrame)
        _onFrame = _services.Scheduler.EveryFrame([this] { OnGameFrame(); });
}

MenuCore::CloseResult MenuCore::Close(int slot)
{
    if (!IsValidSlot(slot))
        return CloseResult::NotOpen;

    // Clear prompts for menus that are closing so they cannot consume later chat input.
    _services.ChatInput.CancelCapture(slot);

    if (!_menus->IsOpen(slot))
        return CloseResult::NotOpen;

    if (!_menus->Pop(slot))
    {
        Log::Info("Menu closed for slot {} ({} left on the stack)", slot, _menus->Depth(slot));
        return CloseResult::Stayed;
    }

    Log::Info("Menu closed for slot {} (0 left on the stack)", slot);
    SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    return CloseResult::Emptied;
}

MenuCore::CloseResult MenuCore::CloseAll(int slot)
{
    if (!IsValidSlot(slot))
        return CloseResult::NotOpen;

    _services.ChatInput.CancelCapture(slot);

    if (!_menus->IsOpen(slot))
        return CloseResult::NotOpen;

    _menus->Clear(slot);
    SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    Log::Info("All menus closed for slot {}", slot);
    return CloseResult::Emptied;
}

void MenuCore::Reply(int slot, std::string_view replyKey)
{
    if (auto& reply = _services.Policy.Reply; reply)
        reply(slot, _services.Translations.Get(std::string(replyKey), slot));
}

void MenuCore::Prompt(int slot, std::string prompt, std::function<bool(int, std::string_view)> callback)
{
    _services.ChatInput.BeginCapture(slot, std::move(prompt), std::move(callback));
}

void MenuCore::CancelPrompt(int slot)
{
    _services.ChatInput.CancelCapture(slot);
}

std::string MenuCore::Translate(int slot, std::string_view key, std::string_view fallback) const
{
    return _services.Translations.GetOr(key, slot, fallback);
}

void MenuCore::FreezeWhileOpen(bool enabled)
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

void MenuCore::EveryFrame(std::function<void()> work)
{
    _work = std::move(work);
}

void MenuCore::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_menus->IsOpen(slot))
            SyncFreeze(slot, _services.Entities.PawnOf(slot));
    }

    if (_work)
        _work();

    // Slot reset clears stacks without going through Close, so stop per-frame work here.
    if (!_menus->AnyOpen())
        _onFrame.Reset();
}

bool MenuCore::HandleKeys(int slot, int rowsPerPage, const std::function<void(int, int)>& showPage)
{
    if (!_menus->Current(slot) || !_menus->KeyboardEnabled(slot))
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

    if (!HandlePressed(slot, pressed, rowsPerPage, showPage))
        return false;

    // The action may have replaced the session.
    _menus->State(slot).LastInputTime = now;
    return true;
}

bool MenuCore::HandlePressed(int slot, uint64_t pressed, int rowsPerPage, const std::function<void(int, int)>& showPage)
{
    if (pressed & IN_RELOAD)
    {
        _host.Close(slot);
        return true;
    }

    Menu* menu = _menus->Current(slot);
    const int itemCount = menu ? static_cast<int>(menu->Items.size()) : 0;
    if (itemCount == 0)
        return false;

    if (pressed & IN_FORWARD)
    {
        MoveCursor(slot, -1, rowsPerPage, showPage);
        return true;
    }
    if (pressed & IN_BACK)
    {
        MoveCursor(slot, +1, rowsPerPage, showPage);
        return true;
    }
    if (pressed & (IN_MOVELEFT | IN_MOVERIGHT))
    {
        const int direction = (pressed & IN_MOVELEFT) ? -1 : +1;
        if (_menus->Step(slot, _menus->Selected(slot), direction))
            return true;
        if (itemCount <= rowsPerPage)
            return false;
        JumpPage(slot, direction, rowsPerPage, showPage);
        return true;
    }
    if (pressed & IN_USE)
    {
        _menus->Activate(slot, _menus->Selected(slot));
        return true;
    }
    return false;
}

void MenuCore::MoveCursor(int slot, int step, int rowsPerPage, const std::function<void(int, int)>& showPage)
{
    if (!_menus->Current(slot))
        return;

    const int index = MenuCursor::Step(_menus->Rows(slot), _menus->Selected(slot), step);
    _menus->Select(slot, index);
    if (showPage)
        showPage(slot, index / rowsPerPage);
}

void MenuCore::JumpPage(int slot, int delta, int rowsPerPage, const std::function<void(int, int)>& showPage)
{
    Menu* menu = _menus->Current(slot);
    if (!menu || menu->Items.empty())
        return;

    const int index = MenuCursor::JumpPage(_menus->Rows(slot), _menus->Selected(slot), rowsPerPage, delta);
    _menus->Select(slot, index);
    if (showPage)
        showPage(slot, index / rowsPerPage);
}

void MenuCore::SetPlayerFrozen(int slot, bool frozen, const Pawn& pawn)
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

void MenuCore::SyncFreeze(int slot, const Pawn& pawn)
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

}  // namespace VoltMod
