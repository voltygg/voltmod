#include "Menu/CenterHtmlRender.hpp"
#include "Menu/MenuCursor.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Menu/CenterHtmlMenu.hpp>
#include <cstddef>
#include <memory>
#include <utility>

namespace VoltMod
{

/** The rows of @p menu as the cursor sees them: how many, and which it may land on. */
static CursorRows CursorRowsFor(Menu* menu, int slot)
{
    if (!menu)
        return {};

    return {.Count = static_cast<int>(menu->Items.size()), .Selectable = [menu, slot](int index) {
                return IsRowActionable(menu->Items[static_cast<std::size_t>(index)], slot);
            }};
}

CenterHtmlMenu::CenterHtmlMenu(const Services& services)
    : _services(services), _stack(*this, _services.Translations, services.Scheduler)
{
    _stack.BindReset(services.Slots);
    _cursors.BindReset(services.Slots);
    _freezes.BindReset(services.Slots);
}

void CenterHtmlMenu::Open(int slot, std::shared_ptr<Menu> menu, MenuOptions options)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    CloseAll(slot);

    _freezes[slot].Requested = options.FreezeMovement;
    if (options.FreezeMovement)
        SetPlayerFrozen(slot, true, _services.Entities.PawnOf(slot));

    Push(slot, std::move(menu));
}

void CenterHtmlMenu::Open(int slot, std::shared_ptr<Menu> menu)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    if (!_stack.IsOpen(slot))
    {
        Open(slot, std::move(menu), {});
        return;
    }

    Push(slot, std::move(menu));
}

void CenterHtmlMenu::Push(int slot, std::shared_ptr<Menu> menu)
{
    _stack.Push(slot, std::move(menu));
    ResetCursor(slot);

    if (auto* current = _stack.Current(slot))
    {
        Log::Info("Menu opened for slot {} (title: {}, depth: {}, items: {})", slot, current->Title, _stack.Depth(slot),
                  current->Items.size());
    }

    if (!_onFrame)
        _onFrame = _services.Scheduler.EveryFrame([this] { OnGameFrame(); });
}

void CenterHtmlMenu::ResetCursor(int slot)
{
    Cursor& cursor = _cursors[slot];
    cursor.LastInputTime = Time::MonotonicMs();
    cursor.Selected = MenuCursor::First(CursorRowsFor(_stack.Current(slot), slot));
}

void CenterHtmlMenu::Select(int slot, int index)
{
    // Ignore stale or client-forged row indexes.
    Menu* menu = _stack.Current(slot);
    if (index < 0 || !menu || index >= static_cast<int>(menu->Items.size()))
        return;

    // Leaving a stepped row applies its pending value. Returning to it leaves the value pending.
    if (!_stack.IsPending(slot, index))
        _stack.ApplyPending(slot);

    _cursors[slot].Selected = index;
}

void CenterHtmlMenu::Close(int slot)
{
    if (!IsValidSlot(slot))
        return;

    // Clear prompts for menus that are closing so they cannot consume later chat input.
    _services.ChatInput.CancelCapture(slot);

    if (!_stack.IsOpen(slot))
        return;

    // A parent menu still showing is drawn next frame.
    _stack.Pop(slot);
    if (_stack.IsOpen(slot))
    {
        ResetCursor(slot);
        Log::Info("Menu closed for slot {} ({} left on the stack)", slot, _stack.Depth(slot));
        return;
    }

    _cursors[slot] = {};
    Log::Info("Menu closed for slot {} (0 left on the stack)", slot);
    SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    _services.Messages.ClearCenterHtml(slot);
}

void CenterHtmlMenu::CloseAll(int slot)
{
    if (!IsValidSlot(slot))
        return;

    _services.ChatInput.CancelCapture(slot);

    if (!_stack.IsOpen(slot))
        return;

    _stack.Clear(slot);
    _cursors[slot] = {};
    SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    Log::Info("All menus closed for slot {}", slot);
    _services.Messages.ClearCenterHtml(slot);
}

void CenterHtmlMenu::CloseAll(int slot, std::string_view replyKey)
{
    // Reply before closing: it is addressed to a player whose menus are about to go.
    if (auto& reply = _services.Policy.Reply; reply)
        reply(slot, _services.Translations.Get(std::string(replyKey), slot));

    CloseAll(slot);
}

void CenterHtmlMenu::Prompt(int slot, std::string prompt, std::function<bool(int, std::string_view)> callback)
{
    _services.ChatInput.BeginCapture(slot, std::move(prompt), std::move(callback));
}

std::string CenterHtmlMenu::Translate(int slot, std::string_view key, std::string_view fallback) const
{
    return _services.Translations.GetOr(key, slot, fallback);
}

bool CenterHtmlMenu::IsOpen(int slot) const
{
    return _stack.IsOpen(slot);
}

void CenterHtmlMenu::FreezeWhileOpen(bool enabled)
{
    _freezeWhileOpen = enabled;

    if (enabled)
        return;

    // Turning it off releases whoever the previous setting already froze; leaving them stuck
    // until they close a menu they may not know is open is not a defensible reading of "off".
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_freezes[slot].Movement)
            SetPlayerFrozen(slot, false, _services.Entities.PawnOf(slot));
    }
}

void CenterHtmlMenu::Draw(int slot)
{
    Menu* menu = _stack.Current(slot);
    if (!menu)
        return;

    // A pending capture replaces the item list with its prompt.
    if (auto prompt = _services.ChatInput.GetPrompt(slot))
    {
        _services.Messages.SendCenterHtml(slot, RenderCaptureOverlay(menu->Title, *prompt));
        return;
    }

    const CenterHtmlView view{
        .Describe = [this, slot](int index) { return _stack.Describe(slot, index); },
        .Breadcrumb = _stack.Breadcrumb(slot),
        .Slot = slot,
        .SelectedIndex = _cursors[slot].Selected,
        .IsSubmenu = _stack.Depth(slot) > 1,
    };
    _services.Messages.SendCenterHtml(slot, RenderMenuHtml(menu, view, _services.Translations));
}

void CenterHtmlMenu::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (!_stack.IsOpen(slot))
            continue;

        SyncFreeze(slot, _services.Entities.PawnOf(slot));
        ReadKeys(slot);

        // Input may have activated a row that closed the menu it was about to draw.
        if (_stack.IsOpen(slot))
            Draw(slot);
    }

    // Slot reset clears stacks without going through Close, so stop per-frame work here.
    if (!_stack.AnyOpen())
        _onFrame.Reset();
}

bool CenterHtmlMenu::ReadKeys(int slot)
{
    if (!_stack.Current(slot))
        return false;

    Cursor& cursor = _cursors[slot];
    const uint64_t buttons = _services.Entities.Buttons(slot);
    const uint64_t pressed = buttons & ~cursor.PrevButtons;
    cursor.PrevButtons = buttons;

    if (pressed == 0)
        return false;

    const int64_t now = Time::MonotonicMs();
    if (now - cursor.LastInputTime < PressGapMs)
        return false;

    if (_services.ChatInput.IsCapturing(slot))
    {
        if ((pressed & IN_RELOAD) == 0)
            return false;

        _services.ChatInput.CancelCapture(slot);
        cursor.LastInputTime = now;
        return true;
    }

    if (!RunKey(slot, pressed))
        return false;

    cursor.LastInputTime = now;
    return true;
}

bool CenterHtmlMenu::RunKey(int slot, uint64_t pressed)
{
    if (pressed & IN_RELOAD)
    {
        Close(slot);
        return true;
    }

    Menu* menu = _stack.Current(slot);
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
        if (_stack.Step(slot, _cursors[slot].Selected, direction))
            return true;
        if (itemCount <= CenterHtmlRowsPerPage)
            return false;
        JumpPage(slot, direction);
        return true;
    }
    if (pressed & IN_USE)
    {
        _stack.Activate(slot, _cursors[slot].Selected);
        return true;
    }
    return false;
}

void CenterHtmlMenu::MoveCursor(int slot, int step)
{
    Select(slot, MenuCursor::Step(CursorRowsFor(_stack.Current(slot), slot), _cursors[slot].Selected, step));
}

void CenterHtmlMenu::JumpPage(int slot, int delta)
{
    Menu* menu = _stack.Current(slot);
    if (!menu || menu->Items.empty())
        return;

    Select(slot,
           MenuCursor::JumpPage(CursorRowsFor(menu, slot), _cursors[slot].Selected, CenterHtmlRowsPerPage, delta));
}

void CenterHtmlMenu::SetPlayerFrozen(int slot, bool frozen, const Pawn& pawn)
{
    // Only the freeze direction is gated. Releasing must always run: gating both meant turning
    // the setting off while sessions were open stranded whoever was already frozen, with no
    // path back short of a reconnect.
    if (frozen && !_freezeWhileOpen)
        return;

    MovementFreeze& movement = _freezes[slot].Movement;
    if (frozen)
        movement.Hold(pawn);
    else
        movement.Release(pawn);
}

void CenterHtmlMenu::SyncFreeze(int slot, const Pawn& pawn)
{
    if (!_freezeWhileOpen || !_freezes[slot].Requested)
        return;

    _freezes[slot].Movement.Sync(pawn);
}

}  // namespace VoltMod
