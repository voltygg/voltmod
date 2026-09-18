# Messages and chat colors {#chat_guide}

[TOC]

```cpp
#include <VoltMod/Api.hpp>

auto& msg = runtime.Messages;

msg.Reply(slot, "Done.");                                   // chat, to one player
msg.Send(slot, "Watch out!", VoltMod::MessageKind::Center);  // plain center print
msg.Broadcast("Server restarting in 5 minutes.");            // chat, to every human player
msg.Broadcast("Round of the day!", VoltMod::MessageKind::Alert);

// Translate for the player's language, substitute tokens, and reply, in one call:
msg.ReplyKey(slot, "cmd.banSuccess", {{"name", targetName}});
```

@ref VoltMod::MessageKind picks the destination: `Chat`, `Center`, `CenterHtml` or `Alert`.
`Reply` is `Send` with `Chat`; `runtime.Policy.Reply` usually forwards to it.
`Shake(slot, durationSec, frequency, amplitude)` shakes one player's view.

Chat output keeps an existing leading color escape or prepends the default, so a line cannot
inherit color from the one before it.

For a sticky center panel that survives the client's HUD clearing use @ref VoltMod::CenterHtml;
see @ref sdk_messaging_guide. This service is main-thread only: call it from hooks, timers,
command handlers or database and HTTP completions, which already run on the game thread.

## Color constants

CS2 treats bytes `0x01` through `0x10` as inline color changes, active until the next escape. The
constants in `<VoltMod/Messaging/ChatColors.hpp>` are `inline constexpr std::string_view`, so they
drop straight into `std::format`.

| Constant(s) | Byte | Color |
| --- | --- | --- |
| `Default` / `White` | `\x01` | White |
| `DarkRed` | `\x02` | Dark red |
| `LightPurple` | `\x03` | Light purple |
| `Green` | `\x04` | Green |
| `Olive` | `\x05` | Olive |
| `Lime` | `\x06` | Lime |
| `Red` | `\x07` | Red |
| `Gray` / `Grey` | `\x08` | Gray |
| `Yellow` / `LightYellow` | `\x09` | Yellow |
| `Silver` / `BlueGrey` | `\x0A` | Silver |
| `LightBlue` / `Blue` | `\x0B` | Light blue |
| `DarkBlue` | `\x0C` | Dark blue |
| `Purple` / `Magenta` | `\x0E` | Purple |
| `LightRed` | `\x0F` | Light red |
| `Gold` / `Orange` | `\x10` | Gold |

Names sharing a byte are aliases. `ChatColors::Palette` is the same set deduplicated to one
canonical lowercase name per byte.

```cpp
namespace ChatColors = VoltMod::ChatColors;

auto line = std::format("{}[ADMIN]{} {}{}{} kicked {} for {}{}",
                        ChatColors::Red, ChatColors::Default,
                        ChatColors::LightBlue, adminName, ChatColors::Default,
                        targetName,
                        ChatColors::Olive, reason);

runtime.Messages.Broadcast(line);
```

For a color that comes from config, look the escape up by name. `ParseNamed` is case-insensitive,
resolves aliases (`"orange"` gives `Gold`), and returns `Default` for anything it does not know:

```cpp
std::string_view color = ChatColors::ParseNamed(group.PrefixColor);
```

`PaletteChoices(labelFor)` returns `(label, canonical name)` pairs for a color picker, shaped for a
`ChoiceRow<std::string>`; `labelFor` supplies each localized label and may return `""` to use the
name itself.

The escape bytes are garbage in a console or a log file, so strip them there:

```cpp
Log::Info("{}", ChatColors::Strip(coloredLine));
```

A repeated broadcast layout ("[PREFIX] actor did-thing target") belongs in the plugin's own chat
service. The framework supplies transport and colors.
