# Messaging and chat input {#sdk_messaging_guide}

[TOC]

## Messages

One service covers every message destination. See @ref chat_guide for colors, translation keys and
broadcasts.

```cpp
auto& msg = runtime.Messages;

msg.Reply(slot, "Hello!");                                 // chat line
msg.Send(slot, "Look up", VoltMod::MessageKind::Center);   // plain center print
msg.Send(slot, "<b>Notice</b>", VoltMod::MessageKind::CenterHtml);
msg.Broadcast("Map change in 60s", VoltMod::MessageKind::Alert);
msg.ReplyKey(slot, "punish.banned", {{"admin", name}});    // translated for the player's language

msg.ClearCenterHtml(slot);
msg.Shake(slot, 1.0f, 40.0f, 8.0f);                        // duration, frequency, amplitude
```

## CenterHtml

CS2 drops center HTML almost immediately after a death, a team switch or a HUD update, so a sticky
panel has to be re-sent. @ref VoltMod::CenterHtml owns that loop and nothing else; the deadline or
expiry policy is yours.

```cpp
// A member of your plugin object; both services belong to the runtime.
VoltMod::CenterHtml panel{runtime.Messages, runtime.Scheduler};

panel.Show(slot, /*refreshMs=*/100, [](int s) {
    return std::format("<b>Time left: {}s</b>", RemainingSeconds(s));  // re-rendered every refresh
});

panel.Stop(slot);   // cancel and clear the panel
```

## ChatInput

A per-slot prompt registry. It powers menu text input and can be used directly.

```cpp
runtime.Hooks.ChatInput.BeginCapture(slot, "Enter your nickname:",
    [](int s, std::string_view text) -> bool {
        if (text.size() > 32)
            return false;                          // re-prompt
        StoreNickname(s, std::string(text));
        return true;                               // accept and clear the capture
    },
    /*timeoutMs=*/30000);
```

| Method | What it does |
| --- | --- |
| `BeginCapture(slot, prompt, callback, timeoutMs = 60000)` | Wait for the slot's next chat line, replacing any pending prompt. A positive timeout cancels the capture. |
| `IsCapturing(slot)` | Whether a prompt is pending. |
| `TryConsume(slot, text)` | Route a chat line to the active prompt. `true` means the caller must suppress the chat broadcast. |
| `CancelCapture(slot)` | Drop the prompt without firing the callback. |
| `GetPrompt(slot)` | A copy of the active prompt as `std::optional<std::string>`. |

The service subscribes to @ref VoltMod::SlotEvents itself, so a pending prompt is cancelled when the
slot changes hands.

`Plugin::OnPlayerChat` already consumes active prompts before dispatching commands. An override
replaces that, so it must call `TryConsume` first or menu text input never completes:

```cpp
bool MyPlugin::OnPlayerChat(Player* p, std::string_view message, bool team) override
{
    if (_app->Runtime.Hooks.ChatInput.TryConsume(p->Slot(), message))
        return true;   // the capture handled it; don't broadcast
    return false;      // fall through to normal chat handling
}
```

## Vote

@ref VoltMod::Vote draws the engine's yes/no panel with user messages and counts the ballots itself,
from the `vote` command the panel's F1/F2 keys send. The panel is the engine's, so the title must be
a `#SFUI_vote...` or `#Panorama_vote...` token the client already has; arbitrary text does not
render. The map's `vote_controller` carries the networked state that makes the client accept F1/F2;
its issue table is never run. Only one vote runs at a time.

```cpp
runtime.Hooks.Vote.StartVote(
    "#SFUI_vote_changelevel",
    "Dust II",                          // the token's detail string
    20.0f,                              // seconds before it closes itself
    callerSlot,                         // whose name the panel credits; -1 for the server
    [](const VoltMod::VoteTally& tally) {
        // Judging on ballots cast rather than everyone connected means abstaining is not a no.
        return tally.Cast() > 0 && tally.Yes * 2 > tally.Cast();
    },
    [](bool passed, VoltMod::VoteEndReason reason) { /* act on the outcome */ });

runtime.Hooks.Vote.InProgress();
runtime.Hooks.Vote.EndVote(VoltMod::VoteEndReason::Cancelled);   // call one off early
```

`StartVote` returns false when a vote is already running or nobody is connected. `VoteEndReason` is
`AllVoted`, `TimeUp` or `Cancelled`. Every callback runs on the game thread.
