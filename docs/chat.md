# Messages and chat colors {#chat_guide}

[TOC]

Use `runtime.Messages` (@ref VoltMod::Messages) for chat, center text,
center HTML, and alert messages. @ref VoltMod::MessageKind selects the
destination. `ChatColors` provides the escape codes used in chat text.

## Sending

```cpp
#include <VoltMod/Api.hpp>

auto& msg = runtime.Messages;

msg.Reply(slot, "Done.");                                  // chat, to one player
msg.Send(slot, "Watch out!", VoltMod::MessageKind::Center); // plain center print
msg.Broadcast("Server restarting in 5 minutes.");          // chat, to everyone
msg.Broadcast("Round of the day!", VoltMod::MessageKind::Alert);

// Translate in the player's language, substitute tokens, and reply, all in one call:
msg.ReplyKey(slot, "cmd.banSuccess", {{"name", targetName}});
```

`Reply` sends a chat message to one player. `runtime.Policy.Reply` usually
forwards to it.

Chat output keeps an existing leading color or prepends the default so it cannot
inherit color from a previous line. CS2 requires `TextMsg` for server-originated
chat and drops `SayText2` from non-player sources.

For a *sticky* center panel that survives the client's aggressive HUD clearing, use @ref VoltMod::CenterHtml; see @ref sdk_messaging_guide.

## Color constants

CS2 treats bytes `0x01` through `0x10` as inline color changes. A color remains
active until the next escape. The constants are `inline constexpr
std::string_view` values suitable for `std::format`:

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

Names sharing a byte are aliases. The byte values follow the current SwiftlyS2
mapping.

## Composing colored text

```cpp
#include <VoltMod/Messaging/ChatColors.hpp>

namespace ChatColors = VoltMod::ChatColors;

auto line = std::format(
    "{}[ADMIN]{} {}{}{} kicked {} for {}{}",
    ChatColors::Red,    ChatColors::Default,
    ChatColors::LightBlue, adminName, ChatColors::Default,
    targetName,
    ChatColors::Olive, reason);

runtime.Messages.Broadcast(line);
```

For runtime/config-driven colors, look the escape up by name. `ParseNamed` is case-insensitive, resolves aliases (`"orange"` → `Gold`), and returns `Default` for unknown names:

```cpp
std::string_view color = ChatColors::ParseNamed(group.PrefixColor);
auto line = std::format("{}{} {}: {}", color, group.Prefix, ChatColors::Default, message);
```

Broadcast layouts with a repeated shape ("[PREFIX] actor did-thing target")
belong in the plugin's chat service. The framework supplies transport and
colors, not an application-specific format.

## Stripping colors for logs

The escape bytes render as colors in-game but are garbage in a console or log file:

```cpp
Log::Info("{}", ChatColors::Strip(coloredLine));
```

## Threading

This service is main-thread only. Call it from hooks, timers, command handlers,
or asynchronous completions (database and HTTP callbacks already run on the
game thread), never from a worker thread created by the plugin.
