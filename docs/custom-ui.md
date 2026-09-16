# Custom UI layouts {#custom_ui_guide}

[TOC]

@ref VoltMod::ScreenManager (`runtime.Screens`) shows CS2 Panorama layouts through
`custom_hud_layout` entities. A layout is compiled XML and CSS and may contain
clickable buttons.

A layout has two parts:

- **Content** - an `.xml` layout and a `.css` stylesheet, compiled and shipped to
  clients. The server never renders it.
- **Control** - the plugin writing text, toggling CSS classes and giving players a
  cursor, through a @ref VoltMod::Screen.

## The shortest working example

```cpp
// App.hpp: the screen owns the entity, so keep it for as long as the layout should live.
VoltMod::Screen _welcome;

// Somewhere in App::Start()
auto screen = runtime.Screens.Shared("welcome");
if (!screen)
    return false;             // the name was refused

_welcome = std::move(*screen);
if (!_welcome.EnsureSpawned(VoltMod::EveryoneSlot))
    return false;             // the engine would not spawn it

_welcome.SetText(VoltMod::EveryoneSlot, "name", "Welcome");
_welcome.SetHidden(VoltMod::EveryoneSlot, "welcome_card", false);  // show it
_welcome.ShowCursor(VoltMod::EveryoneSlot, true);                  // make it clickable

// Later, from a command or an event:
_welcome.SetText(VoltMod::EveryoneSlot, "name", "Round 2");
```

Every write names a slot first. @ref VoltMod::EveryoneSlot is the layout's global
state, what a screen showing everybody the same thing wants; a real slot writes one
player's, which [Per-player content](#custom_ui_per_player) covers.

`SetText` writes the variable a `text="{s:name}"` Label reads. Variables live on the
layout's root element, whose id is the layout's own name, so a text write names only
the variable. `SetClass` names the element by its id.

Each write returns @ref VoltMod::Status. Check one-shot writes. A redraw may ignore
the result: the screen logs the first failure per slot and the next redraw retries.
Unchanged values are not re-sent, so redrawing everything is cheap.

Writes never spawn. @ref VoltMod::Screen::EnsureSpawned spawns the entity on first
use, respawns it after a map change removed it, and respawns when a player who
connected later needs per-player state, so call it before a redraw. Destroying the
screen removes the entity; keep the move-only screen as a member.

Several layouts can exist at once and are independent.

## Authoring the layout

The client validates markup and reports failures in the client console. A
layout must follow these rules:

- Only `Panel`, `Label`, `Image` and `Button`. Anything else is
  `Layout contains disallowed panel type`.
- No `<scripts>` or `<script>` node. This is how "no client-side scripting" is
  enforced, and the node is refused outright.
- Every `Button` needs an `id`. Without one the client logs
  `Button clicked with no id attribute.` and drops the press, so the button looks
  dead for no visible reason.
- Include the stylesheet by its **source** name under `{resources}`, not the
  compiled `.vcss_c` name.

For reliable clicks:

- Every panel on the path to a `Button` needs a resolved size (`width: 100%`, a
  fixed value, or `fill-parent-flow`). A container left to size itself around its
  children renders the buttons in the right place but does not take clicks there.
- Never nest a `Button` inside another `Button`; the inner press is lost. Make
  them siblings and size them side by side.
- `hittest="false"` on decorative panels keeps them from eating clicks meant for
  what is underneath.

```xml
<root>
 <styles>
  <include src="file://{resources}/styles/custom_game/welcome.css" />
 </styles>

 <Panel id="welcome" class="Root">
  <Panel id="welcome_card" class="Hidden">
   <Label class="Title" text="{s:name}" />
   <Panel class="Buttons">
    <Button id="welcome_accept"><Label text="Accept" /></Button>
    <Button id="welcome_decline"><Label text="Decline" /></Button>
   </Panel>
  </Panel>
 </Panel>
</root>
```

Static text needs no variable.

Panorama CSS is not web CSS. Keep selectors flat, do not use `&` or flexbox, and
use `flow-children` for layout. Toggle visibility with a class rather than
swapping layouts:

```css
#welcome_card {
 width: 380px;
 horizontal-align: center;
 flow-children: down;
 transition-property: opacity;
 transition-duration: .1s;
 opacity: 1;
}

#welcome_card.Hidden { visibility: collapse; opacity: 0; }
```

Valve's own reference layout ships as source at
`csgo_addons/cs_script_demo/panorama_stripped/panorama/layout/custom_game/welcome.xml`.
Running `panorama_generate_layout_xsd` on a client dumps the full legal schema
for the build you are on.

## Getting the layout to clients

`m_strLayout` is a **resource name**, not markup, so a client renders only a
layout it already has on disk. The name has to sit under
`panorama/layout/custom_game/` - `gameinfo.gi`'s addon whitelist allows Panorama
layouts nowhere else - and it is spelled with the **source** extension even
though what ships is compiled:

```
welcome                                     expanded to the line below
panorama/layout/custom_game/welcome.xml     correct
panorama/layout/custom_game/welcome.vxml_c  rejected: name the source, not the compiled resource
panorama/layout/hud/welcome.xml             rejected: outside the whitelisted directory
```

@ref VoltMod::ScreenManager::Shared and @ref VoltMod::ScreenManager::ForPlayer
enforce both rules and expand a bare name, so a mistake here is an `Error::Invalid`
rather than a layout that renders nothing and explains itself only on the client
console.

Hand-authoring is one way to get a layout and stylesheet; most screens are
generated instead - see @ref panorama_guide for the Jinja pipeline, the block
library, and the generated C++ header.

`voltmod panorama compile` renders the layouts, runs the CS2 Workshop Tools, and
installs the results in your client:

```bash
uv run poe panorama                          # every plugin that ships a screen
voltmod panorama compile ui-lab              # just one plugin's
voltmod panorama compile --no-deploy         # compile only, leave the client alone
```

It finds the client through Steam's library list; set `CS2_CLIENT_PATH` in
`.env` or pass `--client-path` when that guess is wrong. Sources are staged into
`content/csgo_addons/voltmod/`, compiled to `game/csgo_addons/voltmod/`, and the
compiled resources copied into `csgo/panorama/{layout,styles}/custom_game/`.
Reconnect to see the change; no addon is required. The Workshop Tools run only on
Windows.

To reach *other* players, build a workshop addon with `voltmod panorama compile
--addon NAME --no-deploy`. This leaves your client unchanged. The plugin must
require the published id so joining clients download it.

```cpp
if (auto required = runtime.Addons.Require(3401234567))
    _addon = std::move(*required);   // keep the Subscription; see the workshop guide
```

See @ref workshop_guide for addon requirements and limitations.

## Reacting to a press

A button press arrives as @ref VoltMod::ButtonPress on
@ref VoltMod::ScreenManager::Pressed. Subscribing installs the hook, so keep what
subscribing returns:

```cpp
_subs.Add(runtime.Screens.Pressed += [this](const VoltMod::ButtonPress& press) {
    if (press.ButtonId == "welcome_accept")
        Accept(press.Slot);
});
```

`Pressed` carries presses from every layout, so compare `ButtonId` against ids you
authored; starting every id with the layout name keeps two layouts apart. `ButtonId`
is client-controlled text - never parse anything out of it. `Slot` is the client
that pressed, whatever pawn it is watching.

A press is raised on the game frame after it arrives, not inside the engine's
inbound message processing, so a handler may write to the screen - hide it, drop
the cursor - and the write reaches the client.

Nothing is clickable until that player has a cursor: `ShowCursor(slot, true)`.
Without it the game keeps mouse-look and the layout never sees a pointer - the
usual reason a layout renders but does nothing.

## Per-player content {#custom_ui_per_player}

On a shared screen, passing a slot instead of @ref VoltMod::EveryoneSlot narrows a
write to one player, so one entity can show different content to every player:

```cpp
if (_hud.EnsureSpawned(slot))
    _hud.SetText(slot, "name", player.Name());
```

A client shows the per-player state of the **pawn it is watching**: a spectator
sees the observed player's classes and text, and a spectated player shares theirs
with every spectator. Only the cursor follows the client's own slot. A per-slot
write is therefore that pawn's HUD, and reaches the player themselves only while
they are alive.

## Player screens

A layout one player keeps whatever they are watching - a menu, anything that must
survive death and spectating - is a player screen:

```cpp
auto screen = runtime.Screens.ForPlayer("admin_menu", slot);   // needs runtime.Hooks.Visibility.Available()
if (screen && screen->EnsureSpawned(slot))
    screen->SetText(slot, "title", "Only you see this");
```

The entity is networked to its owner alone through the Visibility filter, and its
text and class writes land in the layout's global state, which a client shows
whatever pawn it is watching. The cursor still goes to the owner's own per-player
state, the one the client reads for itself. Writes name the owner or
@ref VoltMod::EveryoneSlot; any other slot is refused. The screen removes its
entity when the slot changes hands, and `ForPlayer` refuses while the filter is
off.

It costs one entity per player, so create one when something first draws for a
player rather than one per connected player. @ref VoltMod::PlayerScreens does that
for one layout:

```cpp
VoltMod::PlayerScreens _menus{runtime.Screens, "admin_menu"};

VoltMod::Screen& screen = _menus.For(slot);   // created on first use
if (screen.EnsureSpawned(slot))
    screen.SetHidden(slot, "admin_menu", false);
```

A screen `ForPlayer` refuses is logged once and stays empty, so its writes fail
quietly.

Put the writes a screen needs behind a class of your own - `SetRow(slot, index, row)`
reads better at a call site than raw element ids, and the generated header
(@ref panorama_guide) supplies every id it uses. A clickable menu on a player screen
is @ref VoltMod::PanoramaMenu drawing through such a class (see @ref menu_panorama).

## Naming elements and classes

Reuse element ids, class names, and variable names. Each distinct name is
permanently interned in a 1024-entry entity table; generating names during
redraw eventually stops updates.

## Availability

`runtime.Screens.Available()` says whether player screens can be drawn and pressed, and names the
first gamedata entry that did not bind:

| Entry | Missing means |
| --- | --- |
| the five `CCSCustomHudLayout` setters | spawning still works, writes fail |
| `INetworkMessageProcessingPreFilter::FilterMessage`, `CServerSideClient::INetworkMessageProcessingPreFilter` or `CServerSideClientBase::m_nClientSlot` | presses never arrive |
| `CheckTransmitPlayerSlot` | a player screen is refused; shared screens are unaffected (`runtime.Hooks.Visibility.Available()`) |

All are located in `server.dll` / `engine2`, on Windows and on Linux, and stop binding when a
pattern stops matching after a game update - re-verify `gamedata/gamedata.jsonc` before looking
anywhere else.

## Why writes are calls, not netvar pokes

Each of the entity's three networked string tables is shadowed by a server-only
`CUtlHashtable` that is neither in the schema nor networked, and the per-player
state keeps two more. Appending to a vector by hand leaves those indexes stale,
so the next engine-side call misses the hash, appends a duplicate, and the state
silently desyncs. Every write therefore goes through the game's own setter, which
interns, dedupes and notifies correctly.

The one exception is the cursor for @ref VoltMod::EveryoneSlot. No engine setter
takes the global state, and `m_bInputCaptureEnabled` is a plain `bool` in an
embedded struct with no container and no shadow index behind it, so it is written
directly.

Schema fields resolve themselves by name, so an offset that moves in a CS2 update
costs nothing here; the fields declare their expected **size** instead, and a
mismatch warns once at resolve time. What does break is a byte-pattern signature,
which `runtime.Screens.Available()` reports with its reason - check that
first after an update.
