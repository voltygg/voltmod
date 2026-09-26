# Menus {#menus_guide}

[TOC]

A menu is a list of rows and callbacks, independent of how it is drawn.

```cpp
#include <VoltMod/Menu/MenuBuilder.hpp>

using VoltMod::ButtonRow;
using VoltMod::MenuBuilder;
using VoltMod::ToggleRow;

auto menu = MenuBuilder("Admin Panel")
    .Subtitle("v1.0")                                   // optional second line
    .Text("Session")                                    // heading; the cursor skips it
    .Button("Kick Player", [](int slot) { /* ... */ })   // label + callback
    .Add(ButtonRow{.Label = "Disabled", .Enabled = false})
    .Add(ToggleRow{.Label = "God mode", .Get = IsGod, .Flip = FlipGod})
    .Build();

runtime.Menus.OpenSession(playerSlot, menu, {});   // start a session, closing any the player has open
```

@ref VoltMod::MenuSurface::OpenSession starts a session and closes any the player already has; a command
calls it. @ref VoltMod::MenuSurface::Open pushes a submenu onto the open session, which is what a
row calls. `EmptyText("No active bans")` sets the one line a menu draws if nothing else was added,
so a list that filtered down to nothing is not a dead-end page.

## Rows

Each row is a spec struct filled with designated initializers, and `Add` appends it. `Button`,
`Submenu` and `Text` also have two-argument conveniences. `ToItem()` turns a spec into the
@ref VoltMod::MenuItem the menu stores; `Add` calls it for you.

| Spec | What it is |
| --- | --- |
| `TextRow{.Label}` | A heading or divider. Not selectable. |
| `ButtonRow{.Label, .Activate, .Enabled}` | Runs `Activate(slot)` on E or a click. |
| `ToggleRow{.Label, .Get, .Flip, .Enabled}` | Reads `Get` every redraw; E and A/D both run `Flip`. |
| `ChoiceRow<T>{.Label, .Choices, .Commit, .Bind, .Index, .Enabled, .Apply}` | A/D walks the `Labeled<T>` list, wrapping, and applies what it lands on. |
| `InputRow{.Label, .Prompt, .Get, .Set, .MaxLength, .Enabled}` | E routes the player's next chat line into `Set`. |
| `SubmenuRow{.Label, .Build, .Enabled, .Icon}` | Runs `Build(slot)` lazily on E and pushes the result. |

Every `.Enabled` is a @ref VoltMod::EnabledCondition: a `bool`, or a `bool(int slot)` run on every
redraw. It also refuses activation and stepping, so a permission revoked while the menu is open
greys the row out *and* refuses the press. Do not repeat the check inside the handler.

```cpp
.Add(ButtonRow{.Label = "Restart round",
               .Activate = [&](int) { RestartRound(); },
               .Enabled = [&](int slot) { return MayRestart(slot); }})
```

A `ToggleRow` reports its state and none of the words for it; the framework spells those from
`menu.on` / `menu.off`, falling back to `ON` / `OFF`.

A `ChoiceRow` keeps its own index unless `.Bind` puts it somewhere the rest of the menu can read;
the binding carries a getter and a setter together, so a getter alone is not writable. With no
`Commit`, E steps forward like D, which suits a pick-a-value row something else reads live.

```cpp
.Add(ChoiceRow<int>{.Label = "HP", .Choices = {{"1 HP", 1}, {"100 HP", 100}, {"999 HP", 999}},
                    .Commit = [admin, target](int slot, const int& hp) { SetHealth(admin, target, hp); }})
```

Stepping such a row applies it, a moment after the presses stop, so five taps on D are one action.
`.Apply = ChoiceApply::OnActivate` waits for E instead - for a value that must not be tried on the
way past because applying it costs something, cannot be undone, or is announced every time.

`InputRow` re-prompts when `Set` returns false or the line is longer than `MaxLength`; R cancels.
It is backed by @ref VoltMod::ChatInput, so your chat hook must call
`runtime.Hooks.ChatInput.TryConsume` first (see @ref sdk_messaging_guide).

For a shape the specs do not cover, write a @ref VoltMod::MenuItem by hand and `Add` it:
`Describe` is required and runs on every redraw, `Activate` receives the @ref VoltMod::MenuSurface
showing the row (so it can `Open` a submenu or `Prompt` for a line of chat), `Step` consumes A/D,
and `Commit` applies whatever `Step` left showing.

## Context rows

For rows acting on an admin/target pair, build an @ref VoltMod::ActionRows over the services a
press runs through, then append what it returns:

```cpp
using VoltMod::ActionRows;

ActionRows rows({.Actions = app.Actions, .Policy = runtime.Policy,
                 .Translations = runtime.Translations, .Players = runtime.Players,
                 .Entities = runtime.Entities, .Menus = runtime.Menus, .Effects = &app.Effects},
                adminRef, targetRef);            // PlayerRef, optional<PlayerRef>

MenuBuilder(title)
    .Add(rows.Action("action.kill", Actions::Kill))                     // runs an Action
    .Add(rows.StateToggle("action.freeze", IsFrozen, Actions::Freeze))  // bool IsFrozen(const Pawn&)
    .Add(rows.Presets({.LabelKey = "action.health", .Unit = "HP",
                       .Presets = HealthPresets, .Action = Actions::SetHealth}))
    .Add(rows.Effect(Effects::Ghost))           // data-defined effect (EffectDescriptor)
    .Add(rows.EffectPicker(Effects::Model))     // submenu over the effect's Choices
    .Build();
```

These rows translate labels for the admin and use `Policy::Authorize` for their enabled state, then
authorize again on the press, from the stored `PlayerRef` values so slot reuse cannot change the
caller or the target. Objects in `ActionRows::Services` must outlive the rows, which the load cycle
normally guarantees.

`rows.Allows(permission)` hands you that authorization as an @ref VoltMod::EnabledCondition for
your own callbacks, and `rows.Translate(key, tokens)` the admin's wording:

```cpp
.Add(ButtonRow{.Label = rows.Translate("action.callCheck"),
               .Activate = [&](int) { StartCheck(...); },
               .Enabled = rows.Allows("s")})
```

`StateToggle` re-reads its predicate every redraw, so one row shows "Freeze"/"Unfreeze" reality and
doubles as the undo control. `Presets` leaves the menu open after applying, so a value can be
adjusted again. An effect row built without `Services::Effects` is drawn disabled rather than live
and doing nothing; the descriptor itself is in @ref players_guide.

## Flow: multistep wizards

@ref VoltMod::Flow carries a state struct through steps such as "pick duration, pick reason,
confirm, execute". It re-runs validation before each step and before finishing, so a departed
target or a revoked permission aborts cleanly instead of applying half the action. A flow runs for
one player, so every string is a value the caller has already translated.

```cpp
using Flow = VoltMod::Flow<PendingPunishment>;

Flow::Create(runtime.Menus, adminSlot, std::move(pending))
    ->Validate([](const PendingPunishment& s) -> std::optional<std::string> {
        return StillPunishable(s) ? std::nullopt : std::optional<std::string>("cmd.targetLost");
    })
    ->AddDurationStep({.Title = tr("punish.duration"),
                       .Presets = durations,                       // Labeled<int> seconds
                       .Set = [](PendingPunishment& s, int sec) { s.DurationSec = sec; },
                       .CustomLabel = tr("punish.custom"),
                       .CustomPrompt = tr("punish.customPrompt"),
                       .Applies = [](const PendingPunishment& s) { return IsTimed(s.Type); }})
    ->AddOptionsStep({.Title = tr("punish.reason"),
                      .Options = reasons,                          // Labeled<std::string>
                      .Set = [](PendingPunishment& s, const std::string& label, const std::string&) {
                          s.Reason = label;
                      }})
    ->Confirm({.Title = tr("punish.confirmTitle"),
               .Summary = [](const PendingPunishment& s, VoltMod::SummaryRows& rows) {
                   rows.Add(tr("punish.target"), s.TargetName)
                       .AddIf(IsTimed(s.Type), tr("punish.duration"), DurationLabel(s.DurationSec))
                       .Add(tr("punish.reason"), s.Reason);
               }})
    ->Finish([](PendingPunishment& s) { Issue(s); })
    ->Begin();
```

`Create` takes a @ref VoltMod::MenuSurface and a slot and needs no other service, so a flow runs
against a test double. The `Validate` result is a translation key; on failure the flow calls
`MenuSurface::CloseAll(slot, key)`, which replies through `Policy::Reply` and closes the menus.

A step's `Applies` skips it for a state it does not fit, and an empty `CustomLabel` omits that
step's free-text row, so a caller can gate either on config without splitting the chain. Supplying
a `Summary` is what turns the confirm dialog on, and a confirm-only flow may call `Confirm` with no
earlier steps; `SummaryRows::Add(label, value)` renders `"{label}: {value}"`, `Add(label)` the
label alone, and `AddIf(condition, ...)` a line only some states have. `ConfirmLabel` and
`CancelLabel` default to `menu.confirm` / `menu.cancel`.

`AddStep(build, applies)` is the escape hatch for a custom step: build any menu, mutate
`flow.State()`, and call `flow.Advance()`. Menu rows own the flow while one of its menus is open,
so there is no separate cleanup.

## Presets

`<VoltMod/Menu/MenuPresets.hpp>` ships content-agnostic building blocks; every human-facing string
is a parameter.

```cpp
using VoltMod::BuildConfirmMenu;
using VoltMod::BuildDurationMenu;
using VoltMod::BuildPlayerPicker;

// Paginated list of connected players; the optional predicate greys out rows.
auto picker = BuildPlayerPicker(runtime.Players,
    {.Title = "Select player",
     // A row that opens another menu says so, rather than opening one itself.
     .Open = [&](VoltMod::PlayerRef target) { return BuildActionsFor(adminRef, target); },
     .EmptyLabel = "No players available",
     .Enabled = [adminSlot](VoltMod::PlayerRef target) { return target.Slot != adminSlot; }});

// A row per preset length, plus a chat-input row read with ParseDuration
// ("30s", "5m", "2h", "perm"); text it refuses re-prompts.
runtime.Menus.Open(adminSlot, BuildDurationMenu(
    {.Title = "Mute for",
     .Presets = {{"5 minutes", 300}, {"1 hour", 3600}},
     .Pick = [](int slot, int seconds) { Mute(slot, seconds); },
     .CustomLabel = "Custom",
     .CustomPrompt = "Type a duration"}));

// Lines above, then confirm and cancel. Empty labels fall back to "Confirm" and "Cancel";
// an empty Cancel callback closes every menu in the session the dialog is drawn in.
runtime.Menus.Open(adminSlot, BuildConfirmMenu(
    {.Title = "Restart the map?",
     .Lines = {"Map: de_dust2"},
     .Confirm = [](int slot) { RestartMap(slot); }}));
```

`AppendPlayerRows(builder, players, spec)` is the same player list appended into a builder you
already have rows in. A pick reports a @ref VoltMod::PlayerRef, not a slot, because a menu can sit
open across a disconnect - resolve it with `Players.Get(ref)`, which answers with nobody in that
case. Use `.Open` for a pick that opens another menu and `.Pick` for one that just acts; `.Open`
wins if both are set, and returning null pushes nothing.

`Flow::AddDurationStep` and `Flow::Confirm` are these two presets with the flow's state threaded
through them. Colour pickers come from `ChatColors::PaletteChoices(labelFor)`, shaped for a
`ChoiceRow<std::string>` (see @ref chat_guide).

## Feedback and keys {#menu_feedback_keys}

Center HTML is read with one cursor, and the cursor belongs to the session:

| Key | Action |
| --- | --- |
| W / S | Move the cursor up / down, skipping disabled and non-selectable rows |
| E | Activate the row under the cursor |
| A / D | Step a value row (Toggle, Choice); otherwise turn the page |
| R | Close (root) or back (submenu). Cancels an active chat-input capture. |

Presses closer together than 200 ms are ignored. Keys are the only input center HTML has, so there
is no switching them off: a menu nobody can navigate is a menu nobody can close.

A row carrying a `Commit` - a `ChoiceRow`, an `ActionRows::Presets` row - applies what stepping
left it showing once the stepping stops. The commit is held for 400 ms and re-held on each further
step, so a burst of A/D is one action and one broadcast. Activating the row, closing the menu or
moving the cursor off it runs what is held; a player leaving the slot cancels it.

Three pieces of state ride on a row while it is drawn, filled in by @ref VoltMod::MenuStack rather
than by `Describe`:

| State | Means | Center HTML |
| --- | --- | --- |
| Selected | the cursor is on this row | a `>` before the label |
| Changed | the row's value moved in the last 150 ms | `*` after the row |
| Pending | a stepped value is still waiting to be applied | an ellipsis after the value |

A menu longer than one page paginates automatically - five rows for center HTML - with a `(2/3)`
indicator. A/D pages when the row under the cursor has no value to step, so highlight a Button or
Submenu row to page; paging keeps the cursor's offset within the page. A menu carries no markup,
only a `Title`, an optional `Subtitle` and rows that say what they *are*
(@ref VoltMod::MenuRowKind), so styling is a renderer question: center HTML renders from a fixed
palette in `src/Menu/CenterHtmlRender.cpp`.

## Freezing and lifetime

`runtime.Freeze.Enable(true)` holds players still while a menu is open, so WASD does not also walk
them around. The setting is server-wide and every surface honours it. A single session opts out
with `OpenSession(slot, menu, {.FreezeMovement = false})`, which suits menus ordinary players reach
mid-round; @ref VoltMod::MenuOptions applies to the call that opens the stack, and submenus and
Flow steps inherit it.

A session survives death and spectating. Only a live pawn is frozen and only that pawn is restored,
so a respawn is frozen afresh rather than handed a dead body's move type. During a chat-input
capture only R is honored, and the capture belongs to the session that started it, so closing the
menu makes the player's next chat line a chat line again.
@ref VoltMod::CenterHtmlMenu clears each player's stack, cursor, pending commit and movement state
on disconnect; per-frame work starts with the first open menu and stops when the last closes.

## Surfaces

`runtime.Menus` is a @ref VoltMod::MenuRouter over @ref VoltMod::CenterHtmlMenu, the surface every
player can see. @ref VoltMod::PanoramaMenu draws the same sessions on a plugin's own Panorama
screen and takes clicks instead of keys.

```cpp
// App.hpp
VoltMod::PanoramaMenuLayout _layout{runtime.Screens, AdminMenuLayout::Name, AdminMenuLayout::Tabs.size(),
                                     AdminMenuLayout::Rows.size(), AdminMenuLayout::IconSetNames};
VoltMod::Subscription _panorama;                 // after the layout, which the menu draws on

// App::Load; settings.menu is a VoltMod::PanoramaMenuSettings
if (const auto& menu = settings.menu; menu.panorama)
    _panorama = runtime.UsePanorama(_layout, menu.addonId);
```

`UsePanorama` builds the menu from the runtime's services and makes `runtime.Menus` prefer it, for
as long as the returned `Subscription` lives. While it is held, `OpenSession` tries the Panorama menu first and falls back to center HTML
for a player who cannot see the layout - a binding it needs is off, or the client is still
downloading the addon. Every later call follows the surface holding that player's session, and
starting a session closes the one the player had on the other surface. `addonId` is 0 when the
layout is already compiled into the client.

@ref VoltMod::PanoramaMenu draws on a screen built from the `menu` Panorama block, through
@ref VoltMod::PanoramaMenuLayout, which knows element ids and nothing about menus: `Show`/`Hide`,
the `Set*` writes, and `ButtonFor(id)` mapping a pressed id to a @ref VoltMod::MenuButton. The
root menu's submenus become the sidebar tabs. Authoring the screen is @ref panorama_guide.

A screen can pass the block `home` markup, such as a welcome panel. It replaces the rows while
the root menu shows, when the screen panel has the `screen--home` class. The surface writes that
class only for a session opened with `MenuOptions{.HomePage = true}`, so the plugin that owns the
markup asks for it and a menu another plugin opens on the same screen keeps its rows.
`_layout.AddText("home_title", ...)` fills a `{s:home_title}` in that markup, and a Button in
it arrives on `runtime.Screens.Pressed` for the plugin to handle.

A third surface is a class implementing @ref VoltMod::MenuSurface that holds a
@ref VoltMod::MenuStack and forwards to it. The stack owns everything that does not depend on how a
menu is drawn: the open menus, the breadcrumb, `Describe`, `Activate`, `Step`, and the held commit.
Both shipped surfaces hold one, which is why the same @ref VoltMod::Menu behaves the same way on
either. Your class owns the rest: the cursor or click ids, the page shape, freezing and prompts.
A surface that redraws every frame can ignore `MenuStack::Committed`; one that draws on demand
subscribes to it, because a held commit lands on a timer rather than on a press.

## Headers

Include the specific menu headers a translation unit uses, or `<VoltMod/Menu/Api.hpp>` for the full
public surface. `MenuBuilder.hpp`, `Flow.hpp`, `MenuStack.hpp` and
`MenuRouter.hpp` are SDK-free - the two calls a row makes into a live session go through
@ref VoltMod::MenuSurface, an abstract class with no engine behind it - which is what lets the
`tests/Menu/` suite drive real rows and real flows against a fake session. `CenterHtmlMenu.hpp`,
`PanoramaMenu.hpp` and `ActionRows.hpp` are not SDK-free and do not try to be.
