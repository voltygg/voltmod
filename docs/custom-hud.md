# Custom HUD layouts {#custom_hud_guide}

[TOC]

CS2 can render a server-driven Panorama panel through the `custom_hud_layout`
entity: real XML and CSS, with clickable buttons, instead of center HTML.
@ref VoltMod::CustomHud spawns that entity and drives it.

A layout has two halves, and both are needed:

- **Content** - an `.xml` layout and a `.css` stylesheet, compiled and shipped to
  clients. The server never renders it.
- **Control** - the plugin setting dialog variables, toggling CSS classes and
  turning input capture on.

## The shortest working example

```cpp
// App.hpp: what survives between commands is a ref, never the wrapper.
VoltMod::EntityRef _hud;

// Somewhere in App::Start()
auto layout = runtime.World.CustomHud.Spawn("panorama/layout/custom_game/welcome.xml");
if (layout)
{
    _hud = layout->Ref();
    layout->SetText("title", "name", "Welcome");
    layout->SetClass("card", "Hidden", VoltMod::HudClass::Absent);  // show it
    layout->SetInputCapture(true);                                  // make it clickable
}

// Later, from a command or an event:
runtime.World.CustomHud.Get(_hud).SetText("title", "name", "Round 2");
```

@ref VoltMod::HudLayout is a frame-local wrapper like @ref VoltMod::Pawn. Store
the @ref VoltMod::EntityRef and call `Get` where you need it; a stored wrapper
points at freed memory after the entity dies.

Several layouts can exist at once and are independent, so one plugin's HUD does
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

The stylesheet is Panorama CSS, not web CSS: nested rules and `&` work, flexbox
does not - use `flow-children`. The pattern worth copying is to give a panel its
visible state in `#id` and its hidden state in a nested `&.Class`, so showing and
hiding is one `SetClass` call rather than a layout swap:

```css
#card {
 width: 380px;
 horizontal-align: center;
 flow-children: down;
 transition-property: opacity;
 transition-duration: .1s;
 opacity: 1;

 &.Hidden { opacity: 0; }
}
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
panorama/layout/custom_game/welcome.xml     correct
panorama/layout/custom_game/welcome.vxml_c  rejected as an invalid resource name
```

Compile the sources with the CS2 Workshop Tools' `resourcecompiler.exe`, put the
resulting `.vxml_c` and `.vcss_c` in a workshop addon, publish it, and require
its id so joining clients download it:

```cpp
runtime.Addons.Require(3401234567);
```

See @ref workshop_guide for what that costs and what it does not do. During
development it is quicker to drop the compiled files straight into your own
client's `csgo/panorama/layout/custom_game/`.

## Reacting to a click

A button press arrives as @ref VoltMod::HudClick. Subscribing is what installs
the hook, so keep the @ref VoltMod::Subscription:

```cpp
_subs.push_back(runtime.Hooks.HudClicks.Clicked += [this](const VoltMod::HudClick& click) {
    if (click.ButtonId == "accept")
        Accept(click.Slot);
});
```

Nothing is clickable until that player has a cursor, which is
`SetInputCapture(true)`. Without it the game keeps mouse-look and the panel never
sees a pointer - the usual reason a layout renders but does nothing.

`ButtonId` is client-controlled text. Compare it against ids you authored rather
than parsing anything out of it.

## Per-player content

Every method has a `...For(slot, ...)` counterpart writing one player's state,
which the engine networks through a single-slot recipient filter - so one entity
can show different content to every player:

```cpp
layout->SetTextFor(slot, "title", "name", player.Name());
```

The engine's per-player setters index `m_vecPlayerLayoutStates` and return
silently when the slot is past its end, so the `...For` methods check the count
first and fail with a reason rather than looking like they worked. When that
count is zero, the global forms are the ones that work.

## Availability

Ask @ref VoltMod::Capabilities before relying on either feature:

| Capability | Off means |
| --- | --- |
| `CustomHud` | the six `CCSCustomHudLayout` setters did not bind; spawning still works, writes fail |
| `HudClicks` | `FilterMessage` did not bind; presses never arrive |

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

`HudLayout::Describe()` prints every resolved offset against the value it was
verified at. Run it first after a CS2 update: an offset that has moved means the
bound setters are addressing something else.
