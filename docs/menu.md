# Menus {#menus_guide}

[TOC]

A menu is a list of rows and callbacks, independent of its renderer. @ref
VoltMod::MenuBuilder creates common row types, @ref VoltMod::ActionRows builds
admin and target actions, and @ref VoltMod::Flow handles multi-step menus.
`runtime.Menus` owns each player's session and draws it as center HTML: re-sent
every tick, read with WASD / E / R (see @ref menu_feedback_keys), and needing
nothing on the client.

A clickable menu is a plugin's own Panorama screen rather than a framework
feature. The framework ships the pieces - @ref VoltMod::Screen, the widget
writers and the block library - and @ref panorama_guide covers building one.

## What a surface owns, and what it does not {#menu_surface_split}

Only half of a session depends on how it is drawn. @ref VoltMod::MenuStack owns
the other half: the stack of open menus, the breadcrumb, how a row describes
itself, what activating a row does, and how a stepped value is held back so a
burst of presses is one action. Center HTML and a plugin's own screen both hold
one, which is why the same @ref VoltMod::Menu behaves the same way on either.

A surface owns what the stack deliberately leaves out: the cursor or click ids,
the page shape, freezing, and prompts. Build one by implementing
@ref VoltMod::MenuSurface, holding a `MenuStack`, and forwarding to it:

```cpp
class ClickMenu final : public MenuSurface
{
    // Rows call Activate with this surface, so the stack takes *this.
    MenuStack _stack{*this, runtime.Translations, runtime.Scheduler};
};

void ClickMenu::Open(int slot, std::shared_ptr<Menu> menu)  { _stack.Push(slot, std::move(menu)); Draw(slot); }
void ClickMenu::Close(int slot) { _stack.Pop(slot); if (_stack.IsOpen(slot)) Draw(slot); else Hide(slot); }
void ClickMenu::OnPress(int slot, int row) { _stack.Activate(slot, row); Draw(slot); }
```

@ref VoltMod::MenuStack::Describe is what a row is drawn from: it fills in
`Pending` and `Changed` and spells a toggle's on/off word, so a surface never
works those out for itself. A surface that redraws every frame can ignore
@ref VoltMod::MenuStack::Committed; one that draws on demand subscribes to it,
because a held commit lands on a timer rather than on a press.

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
command calls it. The two-argument @ref VoltMod::MenuSurface::Open pushes a submenu
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
@ref VoltMod::MenuSurface showing the row (so it can `Open` a submenu or `Prompt` for a
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

- `Create` takes the @ref VoltMod::MenuSurface and player slot. The flow needs no other service and can run against a test double.
- The `Validate` result is a translation key. On failure the flow calls `MenuSurface::CloseAll(slot, key)`, which replies through `Policy::Reply` and closes the menus.
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

Keys are debounced by 200 ms. They are the only input a menu has, so there is no
switching them off: a menu nobody can navigate is a menu nobody can close.

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

## Lifetime and input

@ref VoltMod::CenterHtmlMenu clears each player's stack, cursor, pending commit,
and movement state on disconnect. Per-frame work starts with the first open menu
and stops when the last stack closes.

`runtime.Menus.FreezeWhileOpen(true)` freezes players while a menu is open, so WASD does not also move them. During a chat-input capture only R is honored, so nobody drifts while they type.

A capture belongs to the session that started it, so closing the menu drops it - the player's next chat line is a chat line again rather than an answer to a prompt nobody can see.

The freeze is a global switch, but a single session can opt out: `Open(slot, menu, {.FreezeMovement = false})`. That suits menus ordinary players reach mid-round, where being held still is worse than the stray movement the freeze prevents. @ref VoltMod::MenuOptions applies to the call that opens the stack; submenus and Flow steps pushed onto a live session inherit it, so an unfrozen session stays unfrozen for its whole flow.

A session survives death and spectating. Only a live pawn is frozen and only that
pawn is restored, so a respawn is frozen afresh rather than handed a dead body's
move type, and keys are read from the pawn the player is driving. All of this
lives on @ref VoltMod::CenterHtmlMenu and its session state.

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
`Open`, an input row's `Prompt`) go through @ref VoltMod::MenuSurface, an abstract
class in `Menu.hpp` with no engine behind it, which also spells the words the framework
supplies for a row through `MenuSurface::Translate`. That is what lets
`tests/Menu/MenuBuilderTests.cpp`, `tests/Menu/FlowTests.cpp` and
`tests/Menu/CenterHtmlRenderTests.cpp` drive real rows and real flows against a fake
session in the SDK-free suite. `CenterHtmlMenu.hpp` and `ActionRows.hpp` are not SDK-free
and do not try to be: the manager freezes a pawn, and an `Action` carries an
`ActionContext` holding a `Controller` by value.
