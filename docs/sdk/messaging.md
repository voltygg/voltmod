# Messaging and chat input {#sdk_messaging_guide}

[TOC]

## MessageSystem

One service handles every destination. See @ref chat_guide for colors,
`ReplyKey`, and broadcast behavior; the raw API is:

```cpp
auto& msg = runtime.Messages;

msg.Reply(slot, "Hello!");                                     // chat line
msg.Send(slot, "Look up", VoltMod::MessageKind::Center);        // plain center print
msg.Send(slot, "<b>Notice</b>", VoltMod::MessageKind::CenterHtml);
msg.Broadcast("Map change in 60s", VoltMod::MessageKind::Alert);

msg.ClearCenterHtml(slot);
```

## PersistentCenterHtml

CS2 drops center-HTML almost immediately after events such as death, a team
switch, or a HUD update. A sticky panel must therefore be sent repeatedly.
@ref VoltMod::Sdk::PersistentCenterHtml owns that refresh loop; the caller owns
the deadline or expiry policy:

```cpp
VoltMod::PersistentCenterHtml panel;

panel.Show(slot, /*refreshMs=*/100, [](int s) {
    return std::format("<b>Time left: {}s</b>", RemainingSeconds(s));  // re-rendered every refresh
});
// ...
panel.Stop(slot);   // cancel + clear the panel
```

## ChatInputCapture

Per-slot pending-prompt registry that backs the menu system's free-text @ref VoltMod::Menu::InputOption. Use it directly when you need a prompt outside of a menu (e.g. a chat command that asks the player to type a value as a follow-up).

```cpp
auto& capture = runtime.ChatInput;

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

Suppressing a chat broadcast has to happen in the `say` / `say_team` hook. With @ref VoltMod::Core::MetamodPlugin that hook is the base's, routed to your `OnPlayerChat` override - call @ref VoltMod::Sdk::ChatInputCapture::TryConsume there and return `true` to supersede:

```cpp
bool MyPlugin::OnPlayerChat(Player* p, std::string_view message, bool team) override
{
    if (runtime.ChatInput.TryConsume(p->GetSlot(), message))
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
| `OnPlayerDisconnect(slot)` | Lifecycle hook - called automatically by `VoltMod::OnPlayerDisconnect`. |
