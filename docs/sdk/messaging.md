# Messaging and chat input {#sdk_messaging_guide}

[TOC]

## Messages

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

## CenterHtml

CS2 drops center-HTML almost immediately after events such as death, a team
switch, or a HUD update. A sticky panel must therefore be sent repeatedly.
@ref VoltMod::CenterHtml owns that refresh loop; the caller owns
the deadline or expiry policy:

```cpp
// A member of your plugin object; the two services it takes belong to the runtime.
VoltMod::CenterHtml panel{runtime.Messages, runtime.Scheduler};

panel.Show(slot, /*refreshMs=*/100, [](int s) {
    return std::format("<b>Time left: {}s</b>", RemainingSeconds(s));  // re-rendered every refresh
});
// ...
panel.Stop(slot);   // cancel + clear the panel
```

## ChatInput

This per-slot prompt registry backs menu text input. Use it directly for prompts
outside a menu.

```cpp
auto& capture = runtime.Hooks.ChatInput;

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

The base `MetamodPlugin::OnPlayerChat` already consumes active prompts before
dispatching commands. An override replaces that behavior, so it must call
@ref VoltMod::ChatInput::TryConsume before handling other chat:

```cpp
bool MyPlugin::OnPlayerChat(Player* p, std::string_view message, bool team) override
{
    if (Rt().Hooks.ChatInput.TryConsume(p->Slot(), message))
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

The service subscribes to @ref VoltMod::SlotEvents itself, so a pending prompt is cancelled
when the slot changes hands. Nothing has to call a lifecycle hook for it.

## Vote

@ref VoltMod::Vote (`runtime.Hooks.Vote`) drives the game's own yes/no vote panel through
the map's `vote_controller` entity. The engine collects the ballots, so there is no plugin-side
tally to keep.

```cpp
runtime.Hooks.Vote.StartVote(
    "#SFUI_vote_changelevel",           // see the token note below
    "Dust II",                          // the token's detail string
    20.0f,                              // seconds before it closes itself
    callerSlot,                         // whose name the panel credits; -1 for the server
    [](const VoltMod::VoteTally& tally) {
        // Decide whether it passed. Judging on ballots cast rather than on everyone connected
        // means abstaining is not the same as voting no.
        return tally.Cast() > 0 && tally.Yes * 2 > tally.Cast();
    },
    [](bool passed, VoltMod::VoteEndReason reason) { /* act on the outcome */ });

runtime.Hooks.Vote.InProgress();                                   // only one vote runs at a time
runtime.Hooks.Vote.EndVote(VoltMod::VoteEndReason::Cancelled);     // call one off early
```

Contracts worth knowing:

- **The title must be a localization token the client already has** - a `#SFUI_vote...` or
  `#Panorama_vote...` string. The panel is the engine's own; arbitrary text does not render.
- There is no arming step: the first `StartVote` subscribes to `vote_cast` itself, so a vote can
  never silently count zero ballots because nobody enabled the service.
- `StartVote` returns false when a vote is already running, when nobody is connected, or when the
  map has no `vote_controller` (which is re-found per vote, since it dies with the map).
- The vote closes on its own as soon as everyone eligible has answered, rather than sitting on a
  decided result until the timer runs out.
- Every callback runs on the game thread.
