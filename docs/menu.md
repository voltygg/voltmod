# Menus {#menus_guide}

[TOC]

A menu is a list of rows and callbacks, independent of its renderer. @ref
VoltMod::MenuBuilder creates common row types, @ref VoltMod::ActionRows builds
admin and target actions, and @ref VoltMod::Flow handles multi-step menus.
`runtime.Menus` owns each player's session and draws it on whichever surface
that player can use: center HTML by default, re-sent every tick and read with
WASD / E / R (see @ref menu_feedback_keys), needing nothing on the client; or,
once @ref VoltMod::MenuManager::UsePanorama has been called, a clickable
Panorama panel for players who can use one - see @ref panorama_menus.

## Building a menu

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

runtime.Menus.Open(playerSlot, menu, {});   // start a session, replacing any the player has open
```

The three-argument `Open` starts a session, closing any the player already has; a
command calls it. The two-argument @ref VoltMod::MenuSession::Open pushes a submenu
onto the open session, which is what a row calls.

Each kind of row is a spec struct filled with designated initializers, and `Add`
appends it. `Button`, `Submenu` and `Text` also have two-argument conveniences for
the common case. Nothing here touches the runtime, so this header - and the `.cpp`
behind it - is SDK-free.

| Spec | What it is |
| --- | --- |
| `TextRow{.Label}` | A heading or divider. Not selectable. |
| `ButtonRow{.Label, .Activate, .Enabled}` | Runs `Activate(slot)` on E or a click. |
| `ToggleRow{.Label, .Get, .Flip, .Enabled}` | Reads `Get` every redraw; E and A/D both run `Flip`. |
| `ChoiceRow<T>{.Label, .Choices, .Commit, .Bind, .Index, .Enabled, .Apply}` | A/D walks the `{label, value}` list (wrapping) and applies what it lands on. |
| `InputRow{.Label, .Prompt, .Get, .Set, .MaxLength, .Enabled}` | E routes the player's next chat line into `Set`. |
| `SubmenuRow{.Label, .Build, .Enabled}` | Runs `Build(slot)` lazily on E and pushes the result. |

`ToItem` consumes the spec, so pass one as a temporary - which the designated-initializer
form above already does.

### Enabled

Every `.Enabled` is a @ref VoltMod::EnabledCondition: a `bool`, or a `bool(int slot)` check
run on every redraw. The check also refuses activation and stepping, so a
permission revoked while the menu is open greys the row out *and* refuses the press:

```cpp
.Add(ButtonRow{.Label = "Restart round",
               .Activate = [&](int) { RestartRound(); },
               .Enabled = [&](int slot) { return MayRestart(slot); }})
```

Do not repeat the same permission check inside the handler.

A `ToggleRow` reports its state and none of the words for it - the framework spells
those from `menu.on` / `menu.off` (falling back to `ON` / `OFF`).

`ChoiceRow` keeps its own index unless `.Bind` puts it somewhere the rest of the menu
can read; the binding carries both halves, so a getter without a setter is not
writable. With no `Commit`, E steps forward like D, which suits a pick-a-value row
something else reads live:

```cpp
.Add(ChoiceRow<int>{.Label = "HP", .Choices = {{"1 HP", 1}, {"100 HP", 100}, {"999 HP", 999}},
                    .Commit = [admin, target](int slot, const int& hp) { SetHealth(admin, target, hp); }})
```

Stepping such a row **applies** it: the value lands a moment after the presses
stop, so five taps on D are one action rather than five (see @ref
menu_feedback_keys). `.Apply = ChoiceApply::OnSelect` takes
that back and waits for E instead - for a value that must not be tried on the way
past, because applying it costs something, cannot be undone, or is announced to
everyone each time it lands.

`InputRow` re-prompts when `Set` returns false or the line is longer than `MaxLength`;
R cancels. It is backed by @ref VoltMod::ChatInput, so your chat hook must call
`runtime.Hooks.ChatInput.TryConsume` first (see @ref sdk_messaging_guide).

`EmptyText` is the one line a menu draws if nothing else was added, so a list that
filtered down to nothing is never a dead-end page:

```cpp
MenuBuilder builder("Active bans");
builder.EmptyText("No active bans");
for (const auto& ban : bans)
    builder.Button(ban.Name, ...);
```

A shape the specs do not cover is a @ref VoltMod::MenuItem written by hand and passed
to `Add`: `Describe` is required and runs on every redraw, `Activate` receives the
@ref VoltMod::MenuSession showing the row (so it can `Open` a submenu or `Prompt` for a
line of chat), `Step` consumes A/D, and `Commit` applies whatever `Step` left showing.

## Context rows

For rows that act on an admin/target pair, build an @ref VoltMod::ActionRows over the
services a row press runs through, then append what it returns:

```cpp
using VoltMod::ActionRows;

ActionRows rows({.Actions = app.Actions, .Policy = runtime.Policy,
                 .Translations = runtime.Translations, .Players = runtime.Players,
                 .Entities = runtime.Entities, .Menus = runtime.Menus, .Effects = &app.Effects},
                adminRef, targetRef);            // PlayerRef, optional<PlayerRef>

MenuBuilder(title)
    .Add(rows.Action("action.kill", Actions::Kill))                     // runs an Action
    .Add(rows.StateToggle("action.freeze", InMoveType(MoveType::None), Actions::Freeze))
    .Add(rows.Presets({.LabelKey = "action.health", .Unit = "HP",
                       .Presets = HealthPresets, .Action = Actions::SetHealth}))
    .Add(rows.Effect(Effects::Ghost))           // data-defined effect (EffectDescriptor)
    .Add(rows.EffectPicker(Effects::Model))     // submenu over the effect's Choices
    .Build();
```

Context rows translate labels for the admin and use `Policy::Authorize` to set
their enabled state. Objects referenced by `ActionRows::Services` must outlive
the rows, which is normally guaranteed by the load cycle.

`rows.Allows(permission)` provides that authorization as a @ref VoltMod::EnabledCondition for
custom callbacks. Context rows authorize again when pressed, using stored `PlayerRef`
values so slot reuse cannot change the caller or target.

```cpp
MenuBuilder(title)
    .Add(ButtonRow{.Label = rows.Tr("action.callCheck"),
                   .Activate = [&](int) { StartCheck(...); },
                   .Enabled = rows.Allows("s")})
```

`StateToggle` re-reads its predicate every redraw, so the same row shows
"Freeze"/"Unfreeze" reality and doubles as the undo control. The pawn predicates
(`InMoveType`, `HasPawnFlag`) live in `Entities/PawnPredicates.hpp`. `Presets` leaves
the menu open after applying, so a value can be adjusted and applied again. An effect
row on a panel built without `Services::Effects` is drawn disabled rather than live and
inert; the descriptor itself is covered in @ref players_guide.

## Flow: multistep wizards

@ref VoltMod::Flow carries a state struct through steps such as "pick duration, pick
reason, confirm, execute". It re-runs validation before each step and before finishing,
so a departed target or revoked permission aborts cleanly instead of applying half the
action. A flow runs for one player, so every string is a value the caller has already
translated.

```cpp
using Flow = VoltMod::Flow<PendingPunishment>;

Flow::Create(runtime.Menus, adminSlot, std::move(pending))
    ->Validate([](const PendingPunishment& s) -> std::optional<std::string> {
        return StillPunishable(s) ? std::nullopt : std::optional<std::string>("cmd.targetLost");
    })
    ->AddDurationStep({.Title = tr("punish.duration"),
                       .Presets = durations,                       // (label, seconds) pairs
                       .Set = [](PendingPunishment& s, int sec) { s.DurationSec = sec; },
                       .CustomLabel = tr("punish.custom"),
                       .CustomPrompt = tr("punish.customPrompt"),
                       .Applies = [](const PendingPunishment& s) { return IsTimed(s.Type); }})
    ->AddOptionsStep({.Title = tr("punish.reason"),
                      .Options = reasons,                          // (label, value) pairs
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
    ->Start();
```

Flow behavior:

- `Create` takes the @ref VoltMod::MenuSession and player slot. The flow needs no other service and can run against a test double.
- The `Validate` result is a translation key. On failure the flow calls `MenuSession::CloseAll(slot, key)`, which replies through `Policy::Reply` and closes the menus.
- A confirm-only flow may call `Confirm` without earlier steps.
- A step's `Applies` skips it for a state it does not fit, and an empty `CustomLabel` omits that step's free-text row, so a caller can gate either on config without splitting the chain.
- Menu rows own the flow while one of its menus is open; no separate cleanup is needed.
- `AddStep(build, applies)` is the escape hatch for a fully custom step: build any menu, mutate `flow.State()`, and call `flow.Advance()`.
- `Summary` fills a @ref VoltMod::SummaryRows: `Add(label, value)` renders `"{label}: {value}"`, `Add(label)` the label alone, and `AddIf(condition, ...)` a line that only some states have. Supplying a `Summary` is what turns the dialog on.
- `ConfirmLabel` and `CancelLabel` default to the session's `menu.confirm` / `menu.cancel` (falling back to `Confirm` / `Cancel`), so name them only for a button that needs its own wording.

## Feedback and keys {#menu_feedback_keys}

Keys drive one cursor, and the cursor belongs to the session:

| Key | Action |
| --- | --- |
| **W** / **S** | Move the cursor up / down, skipping disabled and non-selectable rows |
| **E** | Activate the row under the cursor (button, toggle flip, choice commit, input prompt, ...) |
| **A** / **D** | Step a value row (Toggle / Choice); otherwise turn the page |
| **R** | Close (root) / back (submenu). Cancels an active chat-input capture. |

Keys are debounced by 200 ms. `Open(slot, menu, {.Keyboard = false})` turns the
reading off for one session, which center HTML ignores because a menu nobody can
navigate is a menu nobody can close.

### Stepping applies the value

A row that carries a `Commit` - a `ChoiceRow`, an `ActionRows::Presets` row -
applies what stepping left it showing, once the stepping stops. The commit is
held for 400 ms and re-held on each further step, so a burst of A/D is one
action and one broadcast. Activating the row, closing the menu, moving the cursor
off it runs what is held rather than dropping it; a player
leaving the slot cancels it, because nobody is left to have asked for it.
`ChoiceApply::OnSelect` opts a row out and waits for E.

### What a row says about itself

Three pieces of state ride on a row while it is drawn, filled in by the manager
rather than by the row's own `Describe`:

| State | Means | Center HTML |
| --- | --- | --- |
| Selected | the cursor is on this row | a `>` before the label |
| Changed | the row's value moved in the last 150 ms | `*` after the row |
| Pending | a stepped value is still waiting to be applied | `…` after the value |

## Pagination

A menu longer than one page paginates automatically - five rows for center HTML -
with a `(2/3)` indicator. The page size belongs to the renderer, not to the model.

A/D pages when the row under the cursor has no value to step. Highlight a
Button or Submenu row to page instead. Paging keeps the cursor's offset within the
page.

## Styling

A menu carries no markup - only a `Title`, an optional `Subtitle`, and rows that
say what they *are* (@ref VoltMod::MenuRowKind). The renderer decides what that
looks like, so styling is a renderer question, not a builder one: center HTML
renders from a fixed palette in `src/Menu/CenterHtmlRender.cpp`.

## Panorama menus {#panorama_menus}

```cpp
runtime.Menus.UsePanorama({.Rows = 8, .Nav = 6});
```

Call this once, during startup, to turn on the second surface. From then on, a
session opened for a player who has @ref VoltMod::Capability::CustomUi, @ref
VoltMod::Capability::UiClicks and @ref VoltMod::Capability::Visibility on, has
finished downloading the addon that carries the menu screen
(`Addons.Pending(slot)` is empty), and gets a private panel, draws on a
clickable Panorama panel instead. Everyone else keeps center HTML - nothing on
their end needs the addon. A session already open keeps the surface it started
on; only the next `Open` picks a surface.

If the panel is lost mid-session - the player dropped a capability, say - the
next frame that fails to draw on it moves the session to center HTML for the
rest of that session, forcing `Keyboard` on so the player can still close it.

### Shipping the layout

Panorama draws through the framework's own screen,
`panorama/screens/voltmod_menu.xml.j2` - see @ref panorama_guide. A player needs
it on disk before `UsePanorama` can do anything for them, so the workshop addon
your plugins ship has to carry it:

```bash
voltmod panorama publish DIR voltmod   # copies the framework's rendered screen into your addon content
```

`panorama/skin/voltmod_menu.css` at the project root restyles it without
touching the framework's own template - see @ref panorama_guide_skin.

### Rows and tabs

`.Rows` clamps to the layout's row pool (10). `.Nav` adds a tab strip over the
root menu's submenu rows, clamped to the layout's tab pool (8); `0`, the
default, draws no tabs. A tab press closes back to the root and opens the
submenu it stands for; the tab whose submenu is open is drawn `Selected`.

### Prompts

An open `InputRow` capture is drawn as its own panel with a Cancel button
instead of a placeholder row; the root's `Prompting` class dims the rows and
pager behind it while it is up.

### Wording

Four keys the layout draws for itself, on top of the row-kind wording
`MenuBuilder` and `ActionRows` already use: `nav.back`, `nav.close`,
`nav.cancel`, `menu.promptHint`.

## Lifetime and input

@ref VoltMod::MenuManager clears each player's stack, cursor, pending commit,
and movement state on disconnect. Per-frame work starts with the first open menu
and stops when the last stack closes.

`runtime.Menus.FreezeWhileOpen(true)` freezes players while a menu is open, so WASD does not also move them. During a chat-input capture only R is honored, so nobody drifts while they type.

A capture belongs to the session that started it, so closing the menu drops it - the player's next chat line is a chat line again rather than an answer to a prompt nobody can see.

The freeze is a global switch, but a single session can opt out: `Open(slot, menu, {.FreezeMovement = false})`. That suits menus ordinary players reach mid-round, where being held still is worse than the stray movement the freeze prevents. @ref VoltMod::MenuOptions applies to the call that opens the stack; submenus and Flow steps pushed onto a live session inherit it, so an unfrozen session stays unfrozen for its whole flow. `Keyboard` is the other option it carries, and behaves the same way.

A session survives death and spectating. Only a live pawn is frozen and only that
pawn is restored, so a respawn is frozen afresh rather than handed a dead body's
move type, and keys are read from the pawn the player is driving. All of this
lives on @ref VoltMod::MenuManager and its session state, so it applies the same
whether the session is drawn on center HTML or Panorama.

## Presets

`<VoltMod/Menu/MenuPresets.hpp>` ships content-agnostic building blocks; every
human-facing string is a parameter, and each takes the one service it needs:

```cpp
using VoltMod::BuildPlayerPicker;

// Paginated list of connected players; the optional predicate greys out rows.
auto picker = BuildPlayerPicker(runtime.Players,
    {.Title = "Select player",
     // A row that opens another menu says so, rather than opening one itself.
     .Open = [&](VoltMod::PlayerRef target) { return BuildActionsFor(adminRef, target); },
     .EmptyLabel = "No players available",
     .Enabled = [adminSlot](VoltMod::PlayerRef target) { return target.Slot != adminSlot; }});
```

`AppendPlayerRows(builder, players, spec)` is the same player list appended into a
builder you already have rows in. The viewer is whoever the caller built it for. A pick
reports a @ref VoltMod::PlayerRef rather than a slot - a menu can sit open across a
disconnect, and a bare slot would hand the press to whoever took it - so resolve it with
`Players.Get(ref)`, which answers with nobody in that case.

Use `.Open` for a pick that opens another menu and `.Pick` for one that just acts;
`.Open` wins if both are set, and returning null pushes nothing.

Colour pickers come from the palette itself rather than from this header:
`ChatColors::PaletteChoices(labelFor)` in `<VoltMod/Messaging/ChatColors.hpp>` returns
`(label, canonical name)` pairs shaped for a `ChoiceRow<std::string>`.

Duration pickers and confirm dialogs are presets too, so a plugin can put one in
front of a single action without building a whole `Flow`:

```cpp
using VoltMod::BuildConfirmMenu;
using VoltMod::BuildDurationMenu;

// Rows for each preset length, plus a chat-input row read with ParseDuration
// ("30s", "5m", "2h", "perm"); text it refuses re-prompts.
runtime.Menus.Open(adminSlot, BuildDurationMenu(
    {.Title = "Mute for",
     .Presets = {{"5 minutes", 300}, {"1 hour", 3600}},
     .Pick = [](int slot, int seconds) { Mute(slot, seconds); },
     .CustomLabel = "Custom…",
     .CustomPrompt = "Type a duration"}));

// Lines above, then confirm and cancel. Empty labels fall back to "Confirm" and
// "Cancel"; an empty Cancel callback closes every menu through the session the
// dialog is drawn in.
runtime.Menus.Open(adminSlot, BuildConfirmMenu(
    {.Title = "Restart the map?",
     .Lines = {"Map: de_dust2"},
     .Confirm = [](int slot) { RestartMap(slot); }}));
```

`Flow::AddDurationStep` and `Flow::Confirm` are these two presets with the flow's
state threaded through them.

## Headers

Include the specific menu headers a translation unit uses, or
`<VoltMod/Menu/Api.hpp>` for the full public surface.

`MenuBuilder.hpp`, `Flow.hpp` and the row model behind them are SDK-free: a row is
text and callbacks, and the two calls a row makes into a live session (a submenu's
`Open`, an input row's `Prompt`) go through @ref VoltMod::MenuSession, an abstract
class in `Menu.hpp` with no engine behind it, which also spells the words the framework
supplies for a row through `MenuSession::Translate`. That is what lets
`tests/Menu/MenuBuilderTests.cpp`, `tests/Menu/FlowTests.cpp` and
`tests/Menu/CenterHtmlRenderTests.cpp` drive real rows and real flows against a fake
session in the SDK-free suite. `MenuManager.hpp` and `ActionRows.hpp` are not SDK-free
and do not try to be: the manager freezes a pawn, and an `Action` carries an
`ActionContext` holding a `Controller` by value. `MenuManager.hpp` is also where
`UsePanorama` and `PanoramaMenuOptions` live - see @ref panorama_menus.
