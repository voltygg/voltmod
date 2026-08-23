# Messaging & Chat Input {#sdk_messaging_guide}

[TOC]

## MessageSystem

One service, every destination - see @ref chat_guide for the full messaging story (colors, `ReplyKey`, broadcast semantics). The raw surface:

```cpp
auto& msg = Engine().Messages;

msg.Reply(slot, "Hello!");                                     // chat line
msg.Send(slot, "Look up", CS2Kit::MessageKind::Center);        // plain center print
msg.Send(slot, "<b>Notice</b>", CS2Kit::MessageKind::CenterHtml);
msg.Broadcast("Map change in 60s", CS2Kit::MessageKind::Alert);

msg.ClearCenterHtml(slot);
```

## PersistentCenterHtml

CS2 drops center-HTML almost immediately (death, team switch, HUD updates), so a sticky panel must
be re-sent continuously. @ref CS2Kit::Sdk::PersistentCenterHtml owns that re-send loop; deadline or
expiry policy stays with the caller's own timer:

```cpp
CS2Kit::PersistentCenterHtml panel;

panel.Show(slot, /*refreshMs=*/100, [](int s) {
    return std::format("<b>Time left: {}s</b>", RemainingSeconds(s));  // re-rendered every refresh
});
// ...
panel.Stop(slot);   // cancel + clear the panel
```

## ChatInputCapture

Per-slot pending-prompt registry that backs the menu system's free-text @ref CS2Kit::Menu::InputOption. Use it directly when you need a prompt outside of a menu (e.g. a chat command that asks the player to type a value as a follow-up).

```cpp
auto& capture = Engine().ChatInput;

capture.BeginCapture(slot, "Enter your nickname:",
    [](int s, std::string_view text) -> bool {
        if (text.size() > 32) return false;        // re-prompt
        StoreNickname(s, std::string(text));
        return true;                                // accept
    },
    /*timeoutMs=*/30000);
```

The validator returns `true` to accept the input (capture clears) or `false` to re-prompt the player. The capture auto-cancels after `timeoutMs` of no input.

### Plumbing the chat hook

Suppressing a chat broadcast has to happen in the `say` / `say_team` hook. With @ref CS2Kit::Core::MetamodPlugin that hook is the base's, routed to your `OnPlayerChat` override - call @ref CS2Kit::Sdk::ChatInputCapture::TryConsume there and return `true` to supersede:

```cpp
bool MyPlugin::OnPlayerChat(Player* p, std::string_view message, bool team) override
{
    if (Engine().ChatInput.TryConsume(p->GetSlot(), message))
        return true;   // capture handled it; don't broadcast
    return false;      // fall through to normal chat handling
}
```

If no capture is pending for the slot, `TryConsume` returns `false`.

### API

| Method | Description |
| --- | --- |
| `BeginCapture(slot, prompt, callback, timeoutMs = 30000)` | Start waiting for `slot`'s next chat line. Replaces any previous pending prompt for the same slot. |
| `IsCapturing(slot)` | `true` if `slot` has a pending prompt. |
| `TryConsume(slot, text)` | Route a chat line to the active prompt. Returns `true` when the message was consumed. |
| `CancelCapture(slot)` | Drop the pending prompt without firing the callback. |
| `GetPrompt(slot)` | Returns the active prompt string (used by `MenuRenderer` to draw the overlay), or `nullptr`. |
| `OnPlayerDisconnect(slot)` | Lifecycle hook - called automatically by `CS2Kit::OnPlayerDisconnect`. |
