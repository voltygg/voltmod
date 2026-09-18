# Custom UI layouts {#custom_ui_guide}

[TOC]

@ref VoltMod::ScreenManager (`runtime.Screens`) shows CS2 Panorama layouts through
`custom_hud_layout` entities. The layout itself - compiled XML and CSS - is authored and shipped
separately (@ref panorama_guide); this page is the C++ that drives one.

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

Every write names a slot first. @ref VoltMod::EveryoneSlot is the layout's global state, what a
screen showing everybody the same thing wants; a real slot writes one player's, which
[Per-player content](#custom_ui_per_player) covers.

`SetText` writes the variable a `text="{s:name}"` Label reads. Variables live on the layout's root
element, whose id is the layout's own name, so a text write names only the variable. `SetClass` and
`SetHidden` name an element by its id.

Each write returns @ref VoltMod::Status. Check one-shot writes; a redraw may ignore the result,
since the screen logs the first failure per slot and the next redraw retries. Unchanged values are
not re-sent, so redrawing everything is cheap.

Writes never spawn. @ref VoltMod::Screen::EnsureSpawned spawns the entity on first use, respawns it
after a map change removed it, and respawns when a player who connected later needs per-player
state, so call it before a redraw. `Remove()` drops the entity now and the next `EnsureSpawned`
makes a new one; destroying the move-only `Screen` removes it for good. Several layouts can exist
at once and are independent.

`Shared` and `ForPlayer` take a bare name (`"welcome"`) or a resource name under
`panorama/layout/custom_game/` with its source `.xml` extension. Anything else is an
`Error::Invalid` rather than a layout that renders nothing:

```text
welcome                                     expanded to the line below
panorama/layout/custom_game/welcome.xml     correct
panorama/layout/custom_game/welcome.vxml_c  rejected: name the source, not the compiled resource
panorama/layout/hud/welcome.xml             rejected: outside the whitelisted directory
```

`m_strLayout` is a resource name, not markup, so a client renders only a layout it already has on
disk, and `gameinfo.gi`'s addon whitelist allows Panorama layouts nowhere but that directory.

## Reacting to a press

A button press arrives as @ref VoltMod::ButtonPress on @ref VoltMod::ScreenManager::Pressed.
Subscribing installs the hook, so keep what subscribing returns:

```cpp
_subs.Add(runtime.Screens.Pressed += [this](const VoltMod::ButtonPress& press) {
    if (press.ButtonId == "welcome_accept")
        Accept(press.Slot);
});
```

`Pressed` carries presses from every layout, so compare `ButtonId` against ids you authored;
starting every id with the layout name keeps two layouts apart, which is also what
@ref panorama_guide enforces. `ButtonId` is client-controlled text - never parse anything out of
it. `Slot` is the client that pressed, whatever pawn it is watching.

A press is raised on the game frame after it arrives, not inside the engine's inbound message
processing, so a handler may write to the screen - hide it, drop the cursor - and the write reaches
the client.

Nothing is clickable until that player has a cursor: `ShowCursor(slot, true)`. Without it the game
keeps mouse-look and the layout never sees a pointer, which is the usual reason a layout renders
but does nothing.

## Per-player content {#custom_ui_per_player}

On a shared screen, passing a slot instead of @ref VoltMod::EveryoneSlot narrows a write to one
player, so one entity can show different content to everybody:

```cpp
if (_hud.EnsureSpawned(slot))
    _hud.SetText(slot, "name", player.Name());
```

A client shows the per-player state of the **pawn it is watching**: a spectator sees the observed
player's classes and text, and a spectated player shares theirs with every spectator. Only the
cursor follows the client's own slot. A per-slot write is therefore that pawn's HUD, and reaches
the player themselves only while they are alive.

## Player screens

A layout one player keeps whatever they are watching - a menu, anything that must survive death and
spectating - is a player screen:

```cpp
auto screen = runtime.Screens.ForPlayer("admin_menu", slot);   // needs runtime.Hooks.Visibility.Available()
if (screen && screen->EnsureSpawned(slot))
    screen->SetText(slot, "title", "Only you see this");
```

The entity is networked to its owner alone through the Visibility filter. Its text and class writes
land in the layout's global state, which a client shows whatever pawn it is watching; the cursor
still goes to the owner's own per-player state. Writes name the owner or @ref VoltMod::EveryoneSlot
and any other slot is refused. The screen removes its entity when the slot changes hands, and
`ForPlayer` refuses while the filter is off.

It costs one entity per player, so create one when something first draws for a player rather than
one per connected player. @ref VoltMod::PlayerScreens does that for one layout:

```cpp
VoltMod::PlayerScreens _menus{runtime.Screens, "admin_menu"};

VoltMod::Screen& screen = _menus.For(slot);   // created on first use
if (screen.EnsureSpawned(slot))
    screen.SetHidden(slot, "admin_menu", false);
```

A screen `ForPlayer` refuses is logged once and stays empty, so its writes fail quietly. `Find(slot)`
returns what was already created, or null, and never creates.

Put the writes a screen needs behind a class of your own - `SetRow(slot, index, row)` reads better
at a call site than raw element ids, and the generated header (@ref panorama_guide) supplies every
id it uses. A clickable menu on a player screen is @ref VoltMod::PanoramaMenu drawing through such a
class; see @ref menus_guide.

## Naming elements and classes

Reuse element ids, class names and variable names. Each distinct name is permanently interned in a
1024-entry client-side table, and generating names during a redraw eventually stops updates.
`voltmod panorama check` counts them for you.

## Availability

`runtime.Screens.Available()` says whether screens can be drawn and pressed, and names the first
gamedata entry that did not bind:

| Entry | Missing means |
| --- | --- |
| the five `CCSCustomHudLayout` setters | spawning still works, writes fail |
| `INetworkMessageProcessingPreFilter::FilterMessage`, `CServerSideClient::INetworkMessageProcessingPreFilter` or `CServerSideClientBase::m_nClientSlot` | presses never arrive |
| `CheckTransmitPlayerSlot` | a player screen is refused; shared screens are unaffected (`runtime.Hooks.Visibility.Available()`) |

All are located in `server.dll` / `engine2`, on Windows and on Linux, and stop binding when a
pattern stops matching after a game update - re-verify `gamedata/gamedata.jsonc` before looking
anywhere else. Schema fields resolve themselves by name, so an offset that moves costs nothing
here; the fields declare their expected size instead, and a mismatch warns once at resolve time.

## Why writes are calls, not netvar pokes

Each of the entity's three networked string tables is shadowed by a server-only `CUtlHashtable`
that is neither in the schema nor networked, and the per-player state keeps two more. Appending to
a vector by hand leaves those indexes stale, so the next engine-side call misses the hash, appends
a duplicate, and the state silently desyncs. Every write therefore goes through the game's own
setter, which interns, dedupes and notifies correctly.

The one exception is the cursor for @ref VoltMod::EveryoneSlot. No engine setter takes the global
state, and `m_bInputCaptureEnabled` is a plain `bool` in an embedded struct with no container and
no shadow index behind it, so it is written directly.
