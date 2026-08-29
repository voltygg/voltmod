# Menus {#menus_guide}

[TOC]

A menu is a model: rows and callbacks, no markup. A row is a @ref VoltMod::MenuItem -
four callbacks, no subclass - built from the row specs @ref VoltMod::MenuBuilder takes;
@ref VoltMod::ActionRows and the @ref VoltMod::Flow wizard provide the common
admin-panel behavior.

Nothing in a menu says how it reaches a screen. That is a *driver*, and
@ref VoltMod::MenuManager - `runtime.Menus` - owns both halves: the per-player
session, and the driver drawing it. The same menu runs on either driver, so a
plugin picks once rather than writing two menus.

## Choosing the driver

| | Center HTML | Panorama |
| --- | --- | --- |
| Selected by | the default | `runtime.Menus.UsePanorama(...)` |
| Drawn as | center HTML, re-sent every tick | a Panorama `custom_hud_layout` |
| Input | WASD / E / R | clicks, and the same keys |
| Rows a page | 5 | 8 |
| Needs | nothing | @ref VoltMod::Capability::CustomUi, @ref VoltMod::Capability::UiClicks, and the layout on the client |
| Styling | eight hardcoded colors | a stylesheet you can replace |

**Center HTML works everywhere.** No addon to publish, no capability behind it,
any client. It is what you get without asking, and it stays the fallback when you
can ship content: the custom-UI capability is Windows-only today, so a plugin
that wants the Panorama menu still needs an answer for Linux servers.

**The Panorama menu is a real panel.** Clickable, styled by a stylesheet rather
than by the framework, and cheap to redraw because a networked layout stays on
screen without being re-sent. It costs you getting the compiled layout to
clients - a workshop addon (see @ref workshop_guide), or a manual copy while
developing.

Ask for it once, at load, and carry on either way:

```cpp
if (auto status = runtime.Menus.UsePanorama(layout); !status)
    Log::Info("center HTML: {}", status.error().Detail);
```

`UsePanorama` checks both capabilities and the layout name, and changes nothing
when any of them fails - so the session you already have keeps working. It drives
the layout the framework ships (`voltmod_menu`) when given no name. Both
capabilities are checked because they bind different gamedata and either can be on
while the other is off, and a layout that draws but never reports a press is worse
than no Panorama menu at all.

Switching closes every open session first, because one that straddled two drivers
would leave half its state on the wrong screen. `runtime.Menus.IsPanorama()` and
`runtime.Menus.Layout()` report which driver is drawing.

Both drivers read the same keys, over one cursor - see
[Feedback and keys](#feedback-and-keys). The Panorama menu adds buttons for all
of it (rows, steppers, paging, back and close) and turns the player's cursor on
and off around the session.

## Building a menu

```cpp
#include <VoltMod/Menu/MenuBuilder.hpp>

using VoltMod::ButtonRow;
using VoltMod::MenuBuilder;
using VoltMod::ToggleRow;

auto menu = MenuBuilder("Admin Panel")
    .Subtitle("v1.0")                                   // optional second line; both drivers show it
    .Text("Session")                                    // heading; the cursor skips it
    .Button("Kick Player", [](int slot) { /* ... */ })   // label + callback
    .Add(ButtonRow{.Label = "Disabled", .Enabled = false})
    .Add(ToggleRow{.Label = "God mode", .Get = IsGod, .Flip = FlipGod})
    .Build();

runtime.Menus.Open(playerSlot, menu);
```

Each kind of row is a spec struct filled with designated initializers, and `Add`
appends it. `Button`, `Submenu` and `Text` also have two-argument conveniences for
the common case. Nothing here touches the runtime, so this header - and the `.cpp`
behind it - is SDK-free.

| Spec | What it is |
| --- | --- |
| `TextRow{.Label}` | A heading or divider. Not selectable. |
| `ButtonRow{.Label, .Activate, .Enabled}` | Runs `Activate(slot)` on E or a click. |
| `ToggleRow{.Label, .On, .Off, .Get, .Flip, .Enabled}` | Reads `Get` every redraw; E and A/D both run `Flip`. |
| `ChoiceRow<T>{.Label, .Choices, .Commit, .GetIndex, .SetIndex, .Index, .Enabled, .Apply}` | A/D walks the `{label, value}` list (wrapping) and applies what it lands on. |
| `InputRow{.Label, .Prompt, .Get, .Set, .MaxLength, .Enabled}` | E routes the player's next chat line into `Set`. |
| `SubmenuRow{.Label, .Build, .Enabled}` | Runs `Build(slot)` lazily on E and pushes the result. |

`ChoiceRow` keeps its own index unless `GetIndex`/`SetIndex` put it somewhere the rest
of the menu can read. With no `Commit`, E steps forward like D, which suits a
pick-a-value row something else reads live:

```cpp
.Add(ChoiceRow<int>{.Label = "HP", .Choices = {{"1 HP", 1}, {"100 HP", 100}, {"999 HP", 999}},
                    .Commit = [admin, target](int slot, const int& hp) { SetHealth(admin, target, hp); }})
```

Stepping such a row **applies** it: the value lands a moment after the presses
stop, so five taps on D are one action rather than five (see
[Feedback and keys](#feedback-and-keys)). `.Apply = ChoiceApply::OnSelect` takes
that back and waits for E instead - for a value that must not be tried on the way
past, because applying it costs something, cannot be undone, or is announced to
everyone each time it lands.

`InputRow` re-prompts when `Set` returns false or the line is longer than `MaxLength`;
R cancels. It is backed by @ref VoltMod::ChatInput, so your chat hook must call
`runtime.Hooks.ChatInput.TryConsume` first (see @ref sdk_messaging_guide).

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

Every context row derives its label (a translation key in the admin's language, via
`Tr`), its enabled state (`Allowed`, which is one call to `Policy::Authorize` for the
admin, the target and the row's permission - so a row the admin cannot use renders
disabled) and its dispatch pair from the `ActionRows` that produced it. The services in
`ActionRows::Services` must outlive the rows, which one Load/Unload cycle guarantees.

`Allowed` runs while the row is being built, and only greys the row out: the callback
still has to authorize. A context row does, because it asks `Policy::Authorize` again
**when it is pressed** - against the two `PlayerRef` values, not whoever occupies their
slots by then, so a departed admin's reused slot denies rather than silently authorizing
the new occupant (see @ref players_guide). Anything that must not run without a
permission belongs in an `Action` row, or checks for itself in its callback:

```cpp
MenuBuilder(title)
    .Add(ButtonRow{.Label = rows.Tr("action.callCheck"),
                   .Activate = [&](int) { StartCheck(...); },
                   .Enabled = rows.Allowed("s")})
```

`StateToggle` re-reads its predicate every redraw, so the same row shows
"Freeze"/"Unfreeze" reality and doubles as the undo control. The pawn predicates
(`InMoveType`, `HasPawnFlag`) live in `Entities/PawnPredicates.hpp`. `Presets` leaves
the menu open after applying, so a value can be adjusted and applied again. Effect rows
read on/off labels from the reserved keys `effectState.on` / `effectState.off`; the
descriptor itself is covered in @ref players_guide.

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
    ->Confirm({.Title = tr("punish.confirm"), .Summary = SummaryRows,
               .ConfirmLabel = tr("nav.confirm"), .CancelLabel = tr("nav.cancel")})
    ->Finish([](PendingPunishment& s) { Issue(s); })
    ->Start();
```

Flow contracts:

- `Create` takes the @ref VoltMod::MenuSession the flow opens and closes its steps through, and the slot it runs for, so the flow needs no other service. `runtime.Menus` is one; so is a test double, which is what makes a flow's step order testable without an engine.
- The `Validate` result is a translation key. On failure the flow calls `MenuSession::CloseAll(slot, key)`, which replies through `Policy::Reply` and closes the menus.
- A confirm-only flow (skip straight to `Confirm`) is the natural shape for "quick" variants of a wizard.
- A step's `Applies` skips it for a state it does not fit, and an empty `CustomLabel` omits that step's free-text row, so a caller can gate either on config without splitting the chain.
- Lifetime is automatic: menu rows hold the only owning references, so the flow lives exactly as long as one of its menus is on screen. There is no manager to hold and no cleanup to write.
- `AddStep(build, applies)` is the escape hatch for a fully custom step: build any menu, mutate `flow.State()`, and call `flow.Advance()`.

## Feedback and keys

Both drivers read one key table over one cursor, and the cursor belongs to the
session rather than to whichever driver is drawing it:

| Key | Action |
| --- | --- |
| **W** / **S** | Move the cursor up / down, skipping disabled and non-selectable rows |
| **E** | Activate the row under the cursor (button, toggle flip, choice commit, input prompt, ...) |
| **A** / **D** | Step a value row (Toggle / Choice); otherwise turn the page |
| **R** | Close (root) / back (submenu). Cancels an active chat-input capture. |

Keys are debounced by 200 ms, so a held key steps once per beat instead of
scrolling a menu away. Whether they reach the server at all while a player holds
a Panorama cursor is up to input capture, so nothing depends on them: no key is
simply no change, and the buttons remain the Panorama menu's primary input.
`Open(slot, menu, {.Keyboard = false})` turns the reading off for one Panorama
session; center HTML ignores the option, because a menu nobody can navigate is a
menu nobody can close.

### Stepping applies the value

A row that carries a `Commit` - a `ChoiceRow`, an `ActionRows::Presets` row -
applies what stepping left it showing, once the stepping stops. The commit is
held for 400 ms and re-held on each further step, so a burst of A/D is one
action and one broadcast. Activating the row, closing the menu, moving the cursor
off it, or switching drivers runs what is held rather than dropping it; a player
leaving the slot cancels it, because nobody is left to have asked for it.
`ChoiceApply::OnSelect` opts a row out and waits for E.

### What a row says about itself

Three pieces of state ride on a row while it is drawn, filled in by the manager
rather than by the row's own `Describe`:

| State | Means | Center HTML | Panorama |
| --- | --- | --- | --- |
| Selected | the cursor is on this row | a `>` before the label | `Selected` class |
| Changed | the row's value moved in the last 150 ms | `*` after the row | `Changed` class |
| Pending | a stepped value is still waiting to be applied | `…` after the value | `Pending` class |

The Panorama driver writes them as CSS classes and the stylesheet decides what
they look like; see @ref custom_ui_guide for the whole class vocabulary.

## Pagination

A menu longer than its driver's page paginates automatically - `ItemsPerPage` (5)
rows for center HTML, eight rows for the Panorama menu - with a `(2/3)` indicator.

A/D pages when the row under the cursor has no value to step, so highlight a
Button or Submenu row to page instead. Paging keeps the cursor's offset within the
page. The Panorama menu also has explicit prev/next buttons, and the cursor
follows a page turned that way onto the first row it may land on.

## Styling

A menu carries no markup - only a `Title`, an optional `Subtitle`, and rows that
say what they *are* (@ref VoltMod::MenuRowKind). Each driver decides what that
looks like, so styling is a driver question, not a builder one:

- Center HTML renders from a fixed palette in `src/Menu/CenterHtmlRender.cpp`.
- The Panorama menu puts the row kind and how the row stands on it as CSS classes, so a plugin
  restyles the whole menu by shipping its own stylesheet, or re-lays it out by
  shipping an id-compatible layout and passing its name to
  `Menus.UsePanorama(name)`. See @ref custom_ui_guide for the id contract.

## Lifetime and input

@ref VoltMod::MenuManager keeps the per-player stack, the cursor, the session options, the held-back commits and the freeze bookkeeping, and clears all of it on disconnect. A driver adds only how it draws and which of that input it takes: both read the shared keys every frame, and the Panorama driver also reads clicks. The manager's per-frame subscription is taken by the first `Open` and dropped when the last stack empties, so a plugin with no menu open pays nothing per frame.

`runtime.Menus.FreezeWhileOpen(true)` freezes players while a menu is open. Center HTML needs it so WASD does not also walk them around; the Panorama menu needs it because a cursor takes mouse-look, and being shoved around while clicking is worse rather than better. During a chat-input capture center HTML honors only R, and the Panorama menu shows a prompt overlay and ignores row presses, so neither drifts while the player types.

The freeze is a global switch, but a single session can opt out: `Open(slot, menu, {.FreezeMovement = false})`. That suits menus ordinary players reach mid-round, where being held still is worse than the stray movement the freeze prevents. @ref VoltMod::MenuOptions applies to the call that opens the stack; submenus and Flow steps pushed onto a live session inherit it, so an unfrozen session stays unfrozen for its whole flow. `Keyboard` is the other option it carries, and behaves the same way.

## Presets

`<VoltMod/Menu/MenuPresets.hpp>` ships content-agnostic building blocks; every
human-facing string is a parameter, and each takes the one service it needs:

```cpp
using VoltMod::BuildPaletteChoices;
using VoltMod::BuildPlayerPicker;

// Paginated list of connected players; the optional predicate greys out rows.
auto picker = BuildPlayerPicker(runtime.Players,
    {.Title = "Select player",
     .Pick = [adminSlot](int target) { OpenActionsFor(adminSlot, target); },
     .EmptyLabel = "No players available",
     .Enabled = [adminSlot](int target) { return target != adminSlot; }});

// ChatColors::Palette as ChoiceRow choices for color pickers.
auto choices = BuildPaletteChoices([&](std::string_view name) { return LabelFor(name); });
```

`AppendPlayerRows(builder, players, spec)` is the same player list appended into a
builder you already have rows in. The picked slot is the only thing a `PlayerPicker`
reports; the viewer is whoever the caller built it for, captured in `Pick`.

Duration pickers and confirm dialogs are `Flow`'s own steps (`AddDurationStep`,
`Confirm`), not free functions.

## Headers

Most consumers need `MenuBuilder.hpp` plus `Flow.hpp`, `ActionRows.hpp` or
`MenuPresets.hpp` - or `<VoltMod/Menu/Api.hpp>` for all of them.

`MenuBuilder.hpp`, `Flow.hpp` and the row model behind them are SDK-free: a row is
text and callbacks, and the two calls a row makes into a live session (a submenu's
`Open`, an input row's `Prompt`) go through @ref VoltMod::MenuSession, an abstract
class in `Menu.hpp` with no engine behind it. That is what lets
`tests/Menu/MenuBuilderTests.cpp`, `tests/Menu/FlowTests.cpp` and
`tests/Menu/CenterHtmlRenderTests.cpp` drive real rows and real flows against a fake
session in the SDK-free suite. `MenuManager.hpp` and `ActionRows.hpp` are not SDK-free
and do not try to be: the manager freezes a pawn, and an `Action` carries an
`ActionContext` holding a `Controller` by value.
