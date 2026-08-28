# Custom UI layouts {#custom_ui_guide}

[TOC]

CS2 can render a server-driven Panorama panel through the `custom_hud_layout`
entity: real XML and CSS, with clickable buttons, instead of center HTML.
@ref VoltMod::CustomUi spawns that entity and drives it.

A layout has two halves, and both are needed:

- **Content** - an `.xml` layout and a `.css` stylesheet, compiled and shipped to
  clients. The server never renders it.
- **Control** - the plugin setting dialog variables, toggling CSS classes and
  turning input capture on.

## The shortest working example

```cpp
// App.hpp: the handle owns the entity, so keep it for as long as the panel should live.
VoltMod::UiPanel _panel;

// Somewhere in App::Start()
auto spawned = runtime.Ui.Spawn("welcome");
if (!spawned)
    return false;             // the name was refused, or the engine would not spawn it

_panel = std::move(*spawned);
_panel.SetText("title", "name", "Welcome");
_panel.SetClass("card", "Hidden", false);  // show it
_panel.SetInputCapture(true);              // make it clickable

// Later, from a command or an event:
_panel.SetText("title", "name", "Round 2");
```

@ref VoltMod::UiPanel *owns* its entity: dropping the handle removes the panel. That
is what stops a layout outliving the plugin that spawned it across a
`meta reload`, so keep it as a member of whatever the panel belongs to rather
than storing a bare @ref VoltMod::EntityRef. It is move-only, and it re-resolves
its entity on every call - after a map change it is simply falsy.

Several layouts can exist at once and are independent, so one plugin's panel does
not disturb another's.

## Authoring the layout

The client validates the markup on arrival and prints rejections on the *client*
console, not the server's. Four rules decide whether it renders at all:

- Only `Panel`, `Label`, `Image` and `Button`. Anything else is
  `Layout contains disallowed panel type`.
- No `<scripts>` or `<script>` node. This is how "no client-side scripting" is
  enforced, and the node is refused outright.
- Every `Button` needs an `id`. Without one the client logs
  `Button clicked with no id attribute.` and drops the press, so the button looks
  dead for no visible reason.
- Include the stylesheet by its **source** name under `{resources}`, not the
  compiled `.vcss_c` name.

Three more decide whether its buttons *click*, and each fails silently:

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

`text="{s:name}"` is a dialog variable, which is what `SetText` writes. Static
text needs no variable.

The stylesheet is Panorama CSS, not web CSS: keep selectors flat (no nesting, no
`&`), and there is no flexbox - use `flow-children`. The pattern worth copying is
a visible state in `#id` and a hidden state in `#id.Class`, so showing and hiding
is one `SetClass` call rather than a layout swap:

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

@ref VoltMod::CustomUi::Spawn enforces both rules and expands a bare name, so a
mistake here is an `Error::Invalid` rather than a panel that renders nothing and
explains itself only on the client console.

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

A button press arrives as @ref VoltMod::UiClick. Subscribing is what installs
the hook, so keep the @ref VoltMod::Subscription:

```cpp
_subs.push_back(_panel.OnClick("accept", [this](int slot) { Accept(slot); }));
```

`OnClick` filters on both the layout and the button id, so two layouts that both
have an `accept` button do not trigger each other's handler.
@ref VoltMod::CustomUi::Clicks is the unfiltered form, for a plugin that wants
presses from layouts it did not spawn.

A press is raised on the game frame after it arrives, not from inside the engine's
inbound message processing, so a handler may write to the panel - hide it, release
the cursor - and the write reaches the client.

Nothing is clickable until that player has a cursor, which is
`SetInputCapture(true)`. Without it the game keeps mouse-look and the panel never
sees a pointer - the usual reason a layout renders but does nothing.

`ButtonId` is client-controlled text. Compare it against ids you authored rather
than parsing anything out of it.

## Per-player content

@ref VoltMod::UiPanel::For narrows any write to one player, which the engine
networks through a single-slot recipient filter - so one entity can show
different content to every player:

```cpp
_panel.For(slot).SetText("title", "name", player.Name());
```

The engine's per-player setters index `m_vecPlayerLayoutStates` and return
silently when the slot is past its end, so @ref VoltMod::UiPanel::For checks the
count first and fails with a reason rather than looking like it worked. When that
count is zero, the global forms are the ones that work.

## Reusable blocks

@ref VoltMod::UiPanel is the raw handle: every call reaches the engine, and it is
what you want for a panel you write to once. A layout redrawn every tick wants
the blocks above it instead.

@ref VoltMod::UiLayout owns a panel, re-spawns it when the entity is gone or too
small for the slot being written, and **drops a write whose value the player
already has**. That last part is what makes a per-frame redraw affordable: unlike
center HTML, a networked layout does not need re-sending to stay on screen, so a
frame that changes one row costs one write.

```cpp
UiLayout _layout{runtime.Ui, runtime.Slots, "my_panel"};

if (_layout.EnsureFor(slot))                       // spawns on demand; false means fall back
    _layout.Text(slot, "title", "text", name);     // per player, and only when it changed
```

@ref VoltMod::UiList drives a fixed run of `{prefix}{i}` rows by index and reports
presses back as `(slot, index)`:

| Id | What it is |
| --- | --- |
| `vm_row3` | the row `Panel`; carries `Hidden`, `Disabled`, `HasValue`, `HasSteppers` |
| `vm_row3_btn` | the row's main `Button` |
| `vm_row3_dec`, `vm_row3_inc` | the stepper `Button`s, siblings of the main one |

The row text is two dialog variables on the scope panel - `vm_row3_label` and
`vm_row3_value` - read by `text="{s:vm_row3_label}"` on `Label`s with no ids.

Ids are built once at construction, never per redraw - which matters, because
every distinct panel id, class name and dialog-variable name is interned
permanently into a 1024-entry table on the entity. A generated id set exhausts it.

## Reusing the menu layout

@ref VoltMod::UiMenuManager (see @ref menus_guide) drives
`panorama/layout/custom_game/voltmod_menu.xml`, which ships with the framework and
installs to `addons/voltmod/panorama`. There are three levels of reuse:

1. **Restyle.** Ship your own `voltmod_menu.css`. The server only ever sets
   classes, so `Hidden`, `Disabled`, `HasValue`, `HasSteppers` and the per-kind
   `Kind--text` / `Kind--button` / `Kind--submenu` / `Kind--toggle` /
   `Kind--choice` / `Kind--input` are the whole vocabulary you are styling
   against. No C++ changes.
2. **Re-lay-out.** Ship your own `.xml` declaring the same ids and call
   `runtime.UiMenus.SetLayout("my_menu")`. The contract is the ids below and
   nothing else - the nesting, the artwork and the animation are yours.
3. **Build something else.** Spawn your own @ref VoltMod::UiLayout and bind a
   @ref VoltMod::UiList to your own prefix. Layouts are independent entities, so
   your panel coexists with the admin menu rather than replacing it - which is
   the path for a scoreboard, a welcome card or a vote panel.

The menu layout's id contract (text is dialog variables on `vm_root`:
`vm_title`, `vm_subtitle`, `vm_page`, `vm_prompt_text`, and the per-row
`vm_rowN_label` / `vm_rowN_value`):

| Block | Ids |
| --- | --- |
| root | `vm_root` - `Hidden` in markup, unhidden per viewer |
| header | `vm_subtitle` |
| rows | `vm_row{0..7}` plus `_btn`, `_dec`, `_inc` |
| pager | `vm_pager`, `vm_prev`, `vm_next` |
| nav | `vm_back`, `vm_close` |
| prompt | `vm_prompt`, `vm_cancel` |

The row count must match `UiMenuManager::RowsPerPage` (8). The server cannot read
your layout, so a layout with fewer rows silently loses the ones off the end of a
page.

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

The one exception is the global `SetInputCapture(bool)`: no engine setter takes
the global state, and `m_bInputCaptureEnabled` is a plain `bool` in an embedded
struct with no container and no shadow index behind it, so it is written
directly.

Schema fields resolve themselves by name, so an offset that moves in a CS2 update
costs nothing here; the fields declare their expected **size** instead, and a
mismatch warns once at resolve time. What does break is a byte-pattern signature,
which @ref VoltMod::Capability::CustomUi reports with its reason - check that
first after an update.
