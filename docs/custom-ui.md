# Custom UI layouts {#custom_ui_guide}

[TOC]

@ref VoltMod::CustomUi drives CS2 Panorama panels through
`custom_hud_layout` entities. Panels use compiled XML and CSS and may contain
clickable buttons.

A layout has two required parts:

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

Hand-authoring is one way to get a layout and stylesheet; most screens are
generated instead - see @ref panorama_guide for the Jinja pipeline, the block
library, and the derived C++ binding.

Compiling is `voltmod panorama compile`, which renders first, then runs the CS2
Workshop Tools over every owner's rendered tree and installs the results into
your own client:

```bash
uv run poe panorama                          # every owner this project has
voltmod panorama compile voltmod             # just the framework's own screens
voltmod panorama compile ui-lab              # just one plugin's
voltmod panorama compile --no-deploy         # compile only, leave the client alone
```

It finds the client through Steam's library list; set `CS2_CLIENT_PATH` in
`.env` or pass `--client-path` when that guess is wrong. Sources are staged into
`content/csgo_addons/voltmod/`, compiled to `game/csgo_addons/voltmod/`, and the
compiled resources copied into `csgo/panorama/{layout,styles}/custom_game/` -
which is why a reconnect is enough to see a change, with no addon involved. The
Workshop Tools are Windows only, so this is too.

Reaching *other* players is a workshop addon: `voltmod panorama publish DIR`
copies the rendered tree into an addon content directory for the Workshop Tools
to build from, and the plugin requires the built addon's id so joining clients
download it.

```cpp
_addon = runtime.Addons.Require(3401234567);   // keep the lease; see the workshop guide
```

See @ref workshop_guide for what that costs and what it does not do.

## Reacting to a click

A button press arrives as @ref VoltMod::UiClick. Subscribing is what installs the
hook, so keep what subscribing returns - @ref VoltMod::SubscriptionScope holds several
handlers that live and die together:

```cpp
_subs.Add(_panel.Button("accept") += [this](int slot) { Accept(slot); });
_subs.Add(_panel.Clicked() += [this](const UiClick& click) { Log(click.ButtonId); });
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

A client shows the per-player state of the **pawn it is viewing**: a spectator sees
the observed player's classes and variables, and a spectated player shares theirs
with every spectator. Only input capture follows the viewer's own slot. A per-slot
write is therefore that player's HUD, and reaches the player themselves only while
they are alive.

## Private panels

A panel one player should keep whatever they are looking at - a menu, anything
that must survive death and spectating - is a private panel:

```cpp
auto panel = runtime.Ui.Panel("card", slot);    // needs Capability::Visibility
if (panel && panel->Ensure(slot))
    panel->Text(slot, "title", "name", "Only you see this");
```

The entity is networked to that one client through the Visibility filter, and its
writes land in the layout's global state, which a client shows regardless of the
pawn it is viewing. Input capture still goes to the viewer's own per-player state,
the one the client reads for itself. Writes name the viewer or
@ref VoltMod::UiPanel::Everyone; any other slot is refused. The panel removes its
entity when the slot changes hands, and @ref VoltMod::CustomUi::Panel refuses to
make one while the filter is inert. It costs one entity per viewer, so make one
when something opens rather than one per connected player.

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

## Screens and writers

@ref VoltMod::Screen owns one generated layout: its panel, whether each player
currently has it up, and its Buttons. @ref VoltMod::Screen::Show ensures the panel
for a slot and unhides its root; @ref VoltMod::Screen::Hide hides the root without
tearing the entity down.

Three small writer types turn a layout's ids into typed calls instead of raw
`Text`/`Class` strings:

- @ref VoltMod::Text - one dialog variable on the layout root.
- @ref VoltMod::Flag - one class on one panel, on or off.
- @ref VoltMod::OneOf - one panel, N classes, exactly one of them on (an icon set,
  an accent colour, a bar step). N panels sharing one class - tab selection - is an
  array of `Flag`.

```cpp
constexpr Text CardTitle{.Root = "card", .Var = "title"};
constexpr Flag CardHidden{.Id = "card", .Class = "Hidden"};
constexpr OneOf<3> Icon{.Id = "card_icon", .Classes = {"Icon--ak47", "Icon--awp", "Icon--m4a1"}};

CardTitle.Write(screen.Panel(), slot, "Round 2");
CardHidden.Write(screen.Panel(), slot, false);
Icon.Write(screen.Panel(), slot, Icon.Find("awp"));   // -1 turns every class off
```

You do not write those constants by hand: rendering a screen derives them from its
own ids and classes into `build/panorama/<plugin>/include/Ui/<Screen>.hpp`, which
`voltmod_add_plugin` puts on the plugin's include path. See @ref panorama_guide for
the templates that produce a layout and stylesheet, and the exact derivation rules.

## Naming panels and classes

Reuse panel ids, class names, and dialog-variable names. Each distinct name is
permanently interned in a 1024-entry entity table; generating names during
redraw eventually stops panel updates.

## Availability

Ask @ref VoltMod::Capabilities before relying on either feature:

| Capability | Off means |
| --- | --- |
| `CustomUi` | the five `CCSCustomHudLayout` setters did not bind; spawning still works, writes fail |
| `UiClicks` | `FilterMessage` did not bind; presses never arrive |
| `Visibility` | `CheckTransmitPlayerSlot` is missing; a private panel is refused, shared panels are unaffected |

Both are located by byte pattern in `server.dll` / `engine2`, on Windows and on
Linux. A capability reports off when a pattern stops matching after a game
update - re-verify `gamedata/gamedata.jsonc` before looking anywhere else.

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
