# Custom UI layouts {#custom_ui_guide}

[TOC]

@ref VoltMod::CustomUi drives CS2 Panorama panels through
`custom_hud_layout` entities. Panels use compiled XML and CSS and may contain
clickable buttons.

A layout has two halves, and both are needed:

- **Content** - an `.xml` layout and a `.css` stylesheet, compiled and shipped to
  clients. The server never renders it.
- **Control** - the plugin setting dialog variables, toggling CSS classes and
  turning input capture on.

## The shortest working example

```cpp
// App.hpp: the panel owns the entity, so keep it for as long as the panel should live.
VoltMod::UiPanel _panel;

// Somewhere in App::Start()
auto panel = runtime.Ui.Spawn("welcome");
if (!panel)
    return false;             // the name was refused, or the engine would not spawn it

_panel = std::move(*panel);
_panel.Text(UiPanel::Everyone, "title", "name", "Welcome");
_panel.Class(UiPanel::Everyone, "card", "Hidden", false);  // show it
_panel.InputCapture(UiPanel::Everyone, true);              // make it clickable

// Later, from a command or an event:
_panel.Text(UiPanel::Everyone, "title", "name", "Round 2");
```

Every write names a slot first. @ref VoltMod::UiPanel::Everyone is the layout's
global state, which is what a panel showing everybody the same thing wants; a real
slot writes one player's, which the next section covers.

Each write returns @ref VoltMod::Status. Check one-shot writes. A redraw may
ignore the result because the panel logs the first failure per slot and retries
on the next frame.

@ref VoltMod::UiPanel owns and removes its entity. Keep the move-only panel as a
member instead of storing its @ref VoltMod::EntityRef. Calls re-resolve the
entity, so a panel becomes falsy after a map change.

@ref VoltMod::CustomUi::Spawn creates the entity now. @ref VoltMod::CustomUi::Panel
is the same thing without the entity: it checks the name and hands back a panel
that spawns on its first @ref VoltMod::UiPanel::Ensure, which is what a panel that
only appears when something opens it wants.

Several layouts can exist at once and are independent, so one plugin's panel does
not disturb another's.

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

 <Panel class="Root">
  <Panel id="card" class="Hidden">
   <Label id="title" class="Title" text="{s:name}" />
   <Panel id="buttons">
    <Button id="accept"><Label text="Accept" /></Button>
    <Button id="decline"><Label text="Decline" /></Button>
   </Panel>
  </Panel>
 </Panel>
</root>
```

`text="{s:name}"` is a dialog variable, which is what @ref VoltMod::UiPanel::Text
writes. Static text needs no variable.

Panorama CSS is not web CSS. Keep selectors flat, do not use `&` or flexbox, and
use `flow-children` for layout. Toggle visibility with a class rather than
swapping layouts:

```css
#card {
 width: 380px;
 horizontal-align: center;
 flow-children: down;
 transition-property: opacity;
 transition-duration: .1s;
 opacity: 1;
}

#card.Hidden { visibility: collapse; opacity: 0; }
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

@ref VoltMod::CustomUi::Spawn and @ref VoltMod::CustomUi::Panel enforce both rules
and expand a bare name, so a mistake here is an `Error::Invalid` rather than a panel
that renders nothing and explains itself only on the client console.

Compiling is `voltmod panorama`, which runs the CS2 Workshop Tools over every
`panorama/` directory in the project and installs the results into your own
client:

```bash
uv run poe panorama                 # every layout this project has
uv run poe panorama voltmod         # just the framework's menu layout
uv run poe panorama ui-lab          # just one plugin's
uv run poe panorama --no-deploy     # compile only, leave the client alone
```

It finds the client through Steam's library list; set `CS2_CLIENT_PATH` in
`.env` or pass `--client-path` when that guess is wrong. Sources are staged into
`content/csgo_addons/voltmod/`, compiled to `game/csgo_addons/voltmod/`, and the
compiled resources copied into `csgo/panorama/{layout,styles}/custom_game/` -
which is why a reconnect is enough to see a change, with no addon involved. The
Workshop Tools are Windows only, so this is too.

Reaching *other* players is a workshop addon: put the same compiled files in one,
publish it, and require its id so joining clients download it.

```cpp
_addon = runtime.Addons.Require(3401234567);   // keep the lease; see the workshop guide
```

See @ref workshop_guide for what that costs and what it does not do.

## Reacting to a click

A button press arrives as @ref VoltMod::UiClick. Subscribing is what installs the
hook, so keep what subscribing returns - @ref VoltMod::Subscriptions is the bag for
several handlers that live and die together:

```cpp
_subs.On(_panel.Button("accept"), [this](int slot) { Accept(slot); });
_subs.On(_panel.Clicked(), [this](const UiClick& click) { Log(click.ButtonId); });
```

@ref VoltMod::UiPanel::Button filters on both the layout and the button id, so two
layouts that both have an `accept` button do not trigger each other's handler, and
@ref VoltMod::UiPanel::Clicked is every press in that one layout. Both match on
whichever entity is carrying the layout now, so they survive a re-spawn.
@ref VoltMod::CustomUi::Clicked is the unfiltered form, for a plugin that wants
presses from layouts it did not spawn.

A press is raised on the game frame after it arrives, not from inside the engine's
inbound message processing, so a handler may write to the panel - hide it, release
the cursor - and the write reaches the client.

Nothing is clickable until that player has a cursor, which is
`InputCapture(slot, true)`. Without it the game keeps mouse-look and the panel
never sees a pointer - the usual reason a layout renders but does nothing.

`ButtonId` is client-controlled text. Compare it against ids you authored rather
than parsing anything out of it.

## Per-player content

Passing a slot instead of @ref VoltMod::UiPanel::Everyone narrows a write to one
player, which the engine networks through a single-slot recipient filter - so one
entity can show different content to every player:

```cpp
if (_panel.Ensure(slot))                              // spawns on demand; false means fall back
    _panel.Text(slot, "title", "name", player.Name());
```

The per-player state count is fixed when the entity spawns, so a player who
connected later is only reachable through a new one.
@ref VoltMod::UiPanel::Ensure is where that re-spawn happens, and the only place it
happens: a write never spawns, so call it once before a burst of writes for one
player rather than paying for the check on each. A write for a slot the entity does
not cover fails with a reason instead of looking like it worked, and
@ref VoltMod::UiPanel::Covers asks the same question without the spawn.

Per-player writes are cached, so unchanged values are not resent. @ref
VoltMod::UiPanel::Forget invalidates the cache when another system changes the
panel state.

## Naming panels and classes

Reuse panel ids, class names, and dialog-variable names. Each distinct name is
permanently interned in a 1024-entry entity table; generating names during
redraw eventually stops panel updates.

## Reusing the menu layout

@ref VoltMod::MenuManager's Panorama driver (see @ref menus_guide) drives
`panorama/layout/custom_game/voltmod_menu.xml`, which ships with the framework and
installs to `addons/voltmod/panorama`. There are three levels of reuse:

1. **Restyle.** Ship your own `voltmod_menu.css`. The server only ever sets
   classes, and the table below is the whole vocabulary you are styling against.
   No C++ changes.
2. **Re-lay-out.** Ship your own `.xml` declaring the same ids and call
   `runtime.Menus.UsePanorama("my_menu")`. The contract is the ids below and
   nothing else - the nesting, the artwork and the animation are yours.
3. **Build something else.** Spawn your own @ref VoltMod::UiPanel and write your
   own ids. Layouts are independent entities, so your panel coexists with the admin
   menu rather than replacing it - which is the path for a scoreboard, a welcome
   card or a vote panel.

The menu layout's id contract - what the framework's menu driver writes, and so what
a replacement layout has to declare:

| Block | Ids |
| --- | --- |
| root | `vm_root` - `Hidden` in markup, unhidden per viewer |
| header | `vm_subtitle` |
| rows | `vm_row{0..7}` plus `_btn`, `_dec`, `_inc` |
| pager | `vm_pager`, `vm_prev`, `vm_next` |
| nav | `vm_back`, `vm_close` |
| prompt | `vm_prompt`, `vm_cancel` |

Text arrives as dialog variables, all of them on `vm_root` because a `Label`
resolves `{s:name}` through its ancestors:

| Variable | Carries |
| --- | --- |
| `vm_title` | the menu's title |
| `vm_subtitle` | its second line; `vm_subtitle` the *panel* is `Hidden` when empty |
| `vm_breadcrumb` | the titles this menu was reached through, joined with ` › `; empty at the root |
| `vm_page` | the page counter, always written as `n/m`; the pager panel is `Hidden` when `m` is 1 |
| `vm_prompt_text` | the chat prompt's question |
| `vm_prompt_hint` | how to answer it ("Answer in chat", translation key `menu.promptHint`) |
| `vm_row{i}_label` | the row's name |
| `vm_row{i}_value` | what it is set to |

Each row `vm_row{i}` is a `Panel` carrying the classes below. Its main `Button`
`vm_row{i}_btn` and the steppers `vm_row{i}_dec` and `vm_row{i}_inc` are siblings
of each other, not nested, because a `Button` inside another `Button` loses the
inner press.

| Row class | Set when |
| --- | --- |
| `Hidden` | the row is past the end of the page |
| `Disabled` | the row refuses activation |
| `Selected` | the keyboard cursor is on it (never while the session has keys off) |
| `Changed` | its value moved in the last 150 ms |
| `Pending` | a stepped value is waiting to be applied |
| `HasValue` | it carries a value at all |
| `HasSteppers` | it is a Choice - the only kind that cycles a list |
| `On` | it is a switch that is on |
| `Kind--text` `Kind--button` `Kind--submenu` `Kind--toggle` `Kind--choice` `Kind--input` | what the row is; exactly one is on |

The root carries four of its own:

| Root class | Set when |
| --- | --- |
| `Hidden` | no menu is open for this viewer |
| `Prompting` | a chat prompt is up - row presses are ignored, so dim the rows |
| `KeyHints` | keys drive this session, so the footer's key hints are worth showing |
| `Root` | the stack is one deep, which is what draws Back disabled rather than gone |

`Root` is the new way to say it; `vm_back` still gets `Hidden` at the root too, so
a layout that hides the button keeps working.

An empty menu is drawn as one `Kind--text` row reading "Nothing here"
(translation key `menu.empty`) rather than as a header over blank space.

The row count must match the eight rows the Panorama driver draws a page from. The
server cannot read your layout, so a layout with fewer rows silently loses the ones
off the end of a page.

## Availability

Ask @ref VoltMod::Capabilities before relying on either feature:

| Capability | Off means |
| --- | --- |
| `CustomUi` | the six `CCSCustomHudLayout` setters did not bind; spawning still works, writes fail |
| `UiClicks` | `FilterMessage` did not bind; presses never arrive |

Both are located by byte pattern in `server.dll` / `engine2` and are **Windows
only** today, so they report off on Linux until the patterns are located there.

## Why writes are calls, not netvar pokes

Each of the entity's three networked string tables is shadowed by a server-only
`CUtlHashtable` that is neither in the schema nor networked, and the per-player
state keeps two more. Appending to a vector by hand leaves those indexes stale,
so the next engine-side call misses the hash, appends a duplicate, and the state
silently desyncs. Every write therefore goes through the game's own setter, which
interns, dedupes and notifies correctly.

The one exception is input capture for @ref VoltMod::UiPanel::Everyone. No engine
setter takes the global state, and `m_bInputCaptureEnabled` is a plain `bool` in an embedded
struct with no container and no shadow index behind it, so it is written
directly.

Schema fields resolve themselves by name, so an offset that moves in a CS2 update
costs nothing here; the fields declare their expected **size** instead, and a
mismatch warns once at resolve time. What does break is a byte-pattern signature,
which @ref VoltMod::Capability::CustomUi reports with its reason - check that
first after an update.
