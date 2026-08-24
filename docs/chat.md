# Messages & Chat Colors {#chat_guide}

[TOC]

All player-facing text leaves the server through one service: `runtime.Messages` (@ref VoltMod::Sdk::MessageSystem). It sends to the chat box, the center of the screen, the center-HTML panel, or the alert bar - same call, different @ref VoltMod::Sdk::MessageKind. Colors are plain escape bytes you compose with `std::format` using the `ChatColors` constants.

## Sending

```cpp
#include <VoltMod/Api.hpp>

auto& msg = runtime.Messages;

msg.Reply(slot, "Done.");                                  // chat, to one player
msg.Send(slot, "Watch out!", VoltMod::MessageKind::Center); // plain center print
msg.Broadcast("Server restarting in 5 minutes.");          // chat, to everyone
msg.Broadcast("Round of the day!", VoltMod::MessageKind::Alert);

// Translate in the player's language, substitute tokens, reply - one call:
msg.ReplyKey(slot, "cmd.banSuccess", {{"name", targetName}});
```

`Reply` is `Send(slot, message)` with the chat default - it exists because "reply to the command caller" is the sentence you write most. `runtime.Policy.Reply` typically points straight at it.

Chat sends normalize colors for you: a message that already starts with a color escape keeps it; anything else gets the default color prepended so lines don't inherit the previous line's color. (CS2 routes server-originated chat through `TextMsg` - `SayText2` from non-player sources is silently dropped.)

For a *sticky* center panel that survives the client's aggressive HUD clearing, use @ref VoltMod::Sdk::PersistentCenterHtml - see @ref sdk_messaging_guide.

## Color constants

CS2's chat reads ASCII bytes `0x01`-`0x10` as in-line color toggles. Embed them anywhere; everything until the next escape renders in that color. All constants are `inline constexpr std::string_view`, so they drop into `std::format`:

| Constant(s) | Byte | Color |
|---|---|---|
| `Default` / `White` | `\x01` | White (default) |
| `DarkRed` | `\x02` | Dark red |
| `LightPurple` | `\x03` | Light purple |
| `Green` | `\x04` | Green |
| `Olive` | `\x05` | Olive / dark green |
| `Lime` | `\x06` | Lime / light green |
| `Red` | `\x07` | Red |
| `Gray` / `Grey` | `\x08` | Gray |
| `Yellow` / `LightYellow` | `\x09` | Yellow |
| `Silver` / `BlueGrey` | `\x0A` | Silver / blue-grey |
| `LightBlue` / `Blue` | `\x0B` | Light blue |
| `DarkBlue` | `\x0C` | Dark blue |
| `Purple` / `Magenta` | `\x0E` | Purple / magenta |
| `LightRed` | `\x0F` | Light red |
| `Gold` / `Orange` | `\x10` | Gold / orange |

Names sharing a byte are aliases - pick whichever reads better. Byte values mirror what CS2 actually renders today (per the SwiftlyS2 mapping).

## Composing colored text

```cpp
using namespace VoltMod::Core;

auto line = std::format(
    "{}[ADMIN]{} {}{}{} kicked {} for {}{}",
    ChatColors::Red,    ChatColors::Default,
    ChatColors::LightBlue, adminName, ChatColors::Default,
    targetName,
    ChatColors::Olive, reason);

runtime.Messages.Broadcast(line);
```

For runtime/config-driven colors, look the escape up by name - `ParseNamed` is case-insensitive, resolves aliases (`"orange"` → `Gold`), and returns `Default` for unknown names:

```cpp
std::string_view color = ChatColors::ParseNamed(group.PrefixColor);
auto line = std::format("{}{} {}: {}", color, group.Prefix, ChatColors::Default, message);
```

Broadcast layouts with a repeated shape ("[PREFIX] actor did-thing target") are a few lines of `std::format` in your own chat service - the kit ships transport and colors, not house style.

## Stripping colors for logs

The escape bytes render as colors in-game but are garbage in a console or log file:

```cpp
Log::Info("{}", ChatColors::Strip(coloredLine));
```

## Threading

Main-thread only, like the rest of the kit. Call from hooks, timers, command handlers, or async completions (database/HTTP callbacks already run on the game thread) - never from a worker thread you spawned yourself.
