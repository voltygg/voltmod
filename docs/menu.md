# Menus {#menus_guide}

[TOC]

A menu is a model: rows and callbacks, no markup. Each row is a typed
@ref VoltMod::MenuOption, built with @ref VoltMod::MenuBuilder; context rows and
the @ref VoltMod::Flow wizard provide the common admin-panel behavior.

Nothing in a menu says how it reaches a screen. That is a @ref VoltMod::MenuHost,
and the framework ships two - so the same menu runs on either, and a plugin picks
once rather than writing two menus.

## Which host

| | @ref VoltMod::HtmlMenuManager | @ref VoltMod::UiMenuManager |
| --- | --- | --- |
| Reached as | `runtime.HtmlMenus` | `runtime.UiMenus` |
| Drawn as | center HTML, re-sent every tick | a Panorama `custom_hud_layout` |
| Input | WASD / E / R | mouse clicks |
| Rows a page | 5 | 8 |
| Needs | nothing | @ref VoltMod::Capability::CustomUi, and the layout on the client |
| Styling | eight hardcoded colors | a stylesheet you can replace |

**Center HTML works everywhere.** No addon to publish, no capability behind it,
any client. Pick it when you cannot ship content to players, and keep it as the
fallback when you can: the custom-UI capability is Windows-only today, so a
plugin that wants the Panorama menu still needs an answer for Linux servers.

**The Panorama menu is a real panel.** Clickable, styled by a stylesheet rather
than by the framework, and cheap to redraw because a networked layout stays on
screen without being re-sent. It costs you getting the compiled layout to
clients - a workshop addon (see @ref workshop_guide), or a manual copy while
developing.

Choose once, at load, and pass the result around as a `MenuHost&`:

```cpp
// Center HTML is the fallback, so start there and upgrade: the two managers are unrelated types,
// and a conditional between them would need a cast on each arm to find their common base.
MenuHost* menus = &runtime.HtmlMenus;
if (runtime.Capabilities.Has(Capability::CustomUi))
    menus = &runtime.UiMenus;
```

A session that straddled two hosts would leave half its state on the wrong
screen, so do not switch per menu.

Center-HTML players navigate with:

| Key | Action |
| --- | --- |
| **W** / **S** | Move the cursor up / down |
| **E** | Activate the highlighted row (button, toggle flip, choice commit, input prompt, ...) |
| **A** / **D** | Adjust a value row (Toggle / Choice); otherwise page through a paginated menu |
| **R** | Close (root) / back (submenu). Cancels an active chat-input capture. |

The Panorama menu has no keyboard path at all: rows, steppers, paging and
back/close are all buttons, and a session turns the player's cursor on and off
around itself.

## Building a menu

```cpp
#include <VoltMod/Menu/MenuBuilder.hpp>

using VoltMod::MenuBuilder;

auto menu = MenuBuilder("Admin Panel")
    .Subtitle("v1.0")                                  // optional second line; both hosts show it
    .Button("Kick Player", [](int slot) { /* ... */ })
    .Button("Disabled",    [](int slot) {}, /*enabled=*/false)
    .Build();

menus.OpenMenu(playerSlot, menu);                      // menus is a MenuHost&
```

This plain constructor is enough for any menu whose rows need no player context. Nothing here
touches the runtime, so this header alone (and the `.cpp`s behind it) stays SDK-free.

## Context rows

For rows that act on an admin/target pair, construct the builder with the @ref VoltMod::MenuHost
that will show it, then bind the pair once with `For`:

```cpp
MenuBuilder(menus, title)
    .For(adminRef, targetRef, &app.Effects)                              // PlayerRef, optional<PlayerRef>
    .Row("action.kill", Actions::Kill)                                   // runs an Action
    .StateToggle("action.freeze", InMoveType(MoveType::None), Actions::Freeze)  // live on/off state
    .Presets("action.health", "HP", HealthPresets, Actions::SetHealth)   // A/D cycles, E applies
    .Effect(Effects::Ghost)          // data-defined effect (EffectDescriptor)
    .EffectPicker(Effects::Model)    // submenu over the effect's Choices
    .Build();
```

Every context row derives its label (a translation key in the admin's language, via `Tr`), its
enabled state (`Allowed`, which is one call to `Policy::Authorize` for the admin, the target and
the row's permission - so a row the admin cannot use renders disabled), and its dispatch pair from
the bound `For` state. The `MenuHost` owns a long-lived @ref VoltMod::ActionDispatcher (built
from `Policy`, `PlayerManager` and `EntitySystem`), so a row press runs through it directly - no
throwaway dispatcher is constructed per click.

A builder built with the plain (no-`MenuHost`) constructor is inert for context rows: `Allowed`
denies and `Tr` echoes the key back unresolved, so every context row renders disabled rather than
crashing. `For`'s pair is a `PlayerRef`, not a bare slot: a context row asks `Policy::Authorize` again
**when it is pressed**, against that *same* identity, so a departed admin's old slot being reused
by someone else denies rather than silently authorizing the new occupant - the reason a slot is
ever promoted to a `PlayerRef` in the first place (see @ref players_guide).

The enabled state is a **snapshot**. `Allowed` runs once, while the row is being built, and the
result is stored on the option; nothing recomputes it per redraw. A permission revoked while the
menu is open therefore still shows an enabled row - pressing it is refused, but the row does not
grey out until the menu is rebuilt. Plain `Button` rows gated only by `Allowed` have no
activation check at all, so treat that flag as presentation, not enforcement: anything that must
not run without a permission belongs in an `Action` row (or checks for itself in its callback).

`StateToggle` re-reads its predicate every redraw, so the same row shows "Freeze"/"Unfreeze"
reality and doubles as the undo control. The pawn predicates
(`InMoveType`, `HasPawnFlag`) live in `Entities/PawnPredicates.hpp`. Effect rows read on/off labels
from the reserved keys `effectState.on` / `effectState.off`; the descriptor itself is covered in
@ref players_guide.

A plain row that still needs a permission-gated enabled state (a Button that isn't a single-target
`Action`) uses the same `Allowed` the context rows do:

```cpp
MenuBuilder(menus, title)
    .For(adminRef, std::nullopt)
    .Button(Tr("action.callCheck"), [&](int) { StartCheck(...); }, Allowed("s"))
```

`Allowed` only greys the row out. The callback still has to authorize - admin-system's rows route
theirs through `ActionDispatcher::Resolve`/`Run`, which does it for them.

## Flow: multistep wizards

@ref VoltMod::Flow carries a state struct through steps such as "pick
duration, pick reason, confirm, execute". It re-runs validation before each step
and before finishing, so a departed target or revoked permission aborts cleanly
instead of applying half the action.

```cpp
VoltMod::Flow<PendingPunishment>::Create(menus, std::move(pending))
    ->OnValidate([](int slot, const PendingPunishment& s) -> std::optional<std::string> {
        return StillPunishable(s) ? std::nullopt : std::optional<std::string>("cmd.targetLost");
    })
    ->AddDurationStep(TitleFn, DurationPresetsFn,
                      [](PendingPunishment& s, int sec) { s.DurationSec = sec; },
                      CustomLabelFn, CustomPromptFn,
                      [](const PendingPunishment& s) { return IsTimed(s.Type); })   // skipped for kicks
    ->AddOptionsStep(ReasonTitleFn, ReasonPresetsFn,   // presets are (label, value) pairs
                     [](PendingPunishment& s, const std::string& label, const std::string&) { s.Reason = label; },
                     CustomLabelFn, CustomPromptFn)
    ->WithConfirm(ConfirmTitleFn, SummaryRowsFn, ConfirmLabelFn, CancelLabelFn)
    ->OnFinish([](int slot, PendingPunishment& s) { Issue(slot, s); })
    ->Start(adminSlot);
```

Flow contracts:

- Text comes from per-slot provider functions, so every step renders in the viewing admin's language; the framework ships no strings of its own.
- `Create` takes the @ref VoltMod::MenuHost the flow opens and closes its steps through, so the flow needs no other service.
- The `OnValidate` result is a translation key. On failure the flow calls `MenuHost::CloseAllWithReply`, which replies through `Policy::Reply` and closes the menus.
- A confirm-only flow (skip straight to `WithConfirm`) is the natural shape for "quick" variants of a wizard.
- Lifetime is automatic: menu rows hold the only owning references, so the flow lives exactly as long as one of its menus is on screen. There is no manager to hold and no cleanup to write.
- `AddStep(build, applies)` is the escape hatch for a fully custom step: build any menu, mutate `flow.State()`, and call `flow.Advance(slot)`.

## Option types

Every builder method appends a typed row. Use
`AddOption(std::shared_ptr<MenuOption>)` for a custom subclass.

- `Text(label)` is a non-selectable heading or divider; the cursor skips it.
- `Button(label, onActivate, enabled = true)` is a plain action row.
- `Toggle(title, onLabel, offLabel, getState, onToggle, enabled = true)` carries an on/off value; E and A/D both flip it, and a click or a stepper does on the Panorama menu. State lives wherever you keep it, so pass a getter.
- `Choice<T>(title, choices, onCommit, enabled = true, initialIndex = 0)`: A/D (or the row steppers) cycles the `{label, value}` list and E (or the row itself) commits the current value. The option owns its index, so ephemeral pick-one rows need no external state:

```cpp
.Choice<int>("HP", {{"1 HP", 1}, {"100 HP", 100}, {"999 HP", 999}},
    [admin, target](int slot, const int& hp) {
        Actions::DoSetHealth(admin, target, hp);
        menus.CloseAllMenus(slot);
    })
```

  The getter/setter overload (`Choice<T>(title, choices, getIndex, setIndex, onCommit, enabled)`) remains for state that lives outside the menu. With no `onCommit`, E advances like D, which suits pick-a-value rows another part of the menu reads live.

- `Input(title, prompt, get, set, maxLength = 64, enabled = true)`: E pauses the menu and routes the player's next chat line into `set`; return `false` to re-prompt, `true` to accept. R cancels. Backed by @ref VoltMod::ChatInput, so your chat hook must call `runtime.Hooks.ChatInput.TryConsume` first (see @ref sdk_messaging_guide).
- `Submenu(label, factory, enabled = true)` runs the factory lazily on E and pushes the returned menu onto the stack; R pops back.

## Pagination

A menu longer than its host's page paginates automatically - `ItemsPerPage` (5)
rows for center HTML, `UiMenuManager::RowsPerPage` (8) for the Panorama menu -
with a `(2/3)` indicator.

Center HTML pages with A/D, item-aware: on a value row A/D adjusts the value, so
highlight a Button or Submenu row to page instead. Disabled and non-selectable
rows are skipped by the cursor. The Panorama menu has explicit prev/next buttons
and keeps the page in `PlayerMenuState::Page`, because a click UI has no cursor
to derive a page from.

## Styling

A menu carries no markup - only a `Title`, an optional `Subtitle`, and rows that
say what they *are* (@ref VoltMod::MenuRowKind). Each host decides what that
looks like, so styling is a host question, not a builder one:

- Center HTML renders from a fixed palette in `MenuRenderer`.
- The Panorama menu puts the row kind on the row as a CSS class, so a plugin
  restyles the whole menu by shipping its own stylesheet, or re-lays it out by
  shipping an id-compatible layout and calling `UiMenus.SetLayout(name)`. See
  @ref custom_ui_guide for the id contract.

## Lifetime and input

@ref VoltMod::MenuHost keeps the per-player stack, the session options and the freeze bookkeeping, and clears a player's stack on disconnect. Each host adds only how it draws and what input it reads: @ref VoltMod::HtmlMenuManager reads button state every frame from a self-registered scheduler pump and debounces it (200 ms); @ref VoltMod::UiMenuManager redraws from its own pump and reads clicks.

`menus.SetFreezePlayer(true)` freezes players while a menu is open. Center HTML needs it so WASD does not also walk them around; the Panorama menu needs it because a cursor takes mouse-look, and being shoved around while clicking is worse rather than better. During a chat-input capture center HTML honors only R, and the Panorama menu shows a prompt overlay and ignores row presses, so neither drifts while the player types.

The freeze is a global switch, but a single session can opt out: `OpenMenu(slot, menu, {.FreezeMovement = false})`. That suits menus ordinary players reach mid-round, where being held still is worse than the stray movement the freeze prevents. @ref VoltMod::MenuSessionOptions applies to the call that opens the stack; submenus and Flow steps pushed onto a live session inherit it, so an unfrozen session stays unfrozen for its whole flow.

## Presets

`<VoltMod/Menu/MenuPresets.hpp>` ships content-agnostic building blocks; every human-facing string is a parameter, and each preset takes the one service it needs as its first argument:

```cpp
using VoltMod::BuildDurationPicker;
using VoltMod::BuildPlayerPicker;

// Paginated list of connected players; the optional predicate grays out rows.
auto picker = BuildPlayerPicker(runtime.Players, adminSlot, "Select player",
    [](int viewer, int target) { OpenActionsFor(viewer, target); },
    "No players available",
    [self = adminSlot](int target) { return target != self; });

// Duration presets + optional free-text row parsed by Utils::ParseDuration.
auto duration = BuildDurationPicker(adminSlot, "Ban duration",
    {{"30 min", 1800}, {"1 day", 86400}, {"Permanent", 0}},
    [](int viewer, int seconds) { IssueBan(viewer, seconds); },
    "Custom...", "Type a duration (30s, 5m, 2h, 7d, perm)");

// Confirmation dialog: read-only body rows, then confirm/cancel.
auto confirm = BuildConfirmDialog(menus, {
    .Title = "Confirm: Ban",
    .BodyLines = {"Player: Bob", "Duration: 1 day"},
    .ConfirmLabel = "Confirm",
    .CancelLabel = "Cancel",
    .OnConfirm = [](int slot) { IssueAndClose(slot); },
});

// ChatColors::Palette as choice rows for color pickers.
auto choices = BuildPaletteChoices([&](std::string_view name) { return LabelFor(name); });
```

`Flow` composes these same presets internally. Reach for the raw presets when a single picker is all you need.

## Headers

Most consumers only need `MenuBuilder.hpp` (which pulls in every option type) plus `Flow.hpp` or `MenuPresets.hpp`. The per-option headers under `Menu/Options/` matter only when constructing an option manually for `AddOption` or subclassing `MenuOption` (override `Describe`, `OnActivate(int slot, MenuHost& menus)`, and optionally `OnHorizontal`). `Describe` returns a @ref VoltMod::MenuRow - the row's label, its value and its kind, as plain text - which is what lets a host that cannot read HTML render it. `OnActivate` receives the host showing the row, so a custom option can push a submenu (`menus.OpenMenu`) or start a chat prompt (`menus.BeginInput`) without holding the runtime.

`MenuBuilder.hpp` only forward-declares `MenuHost` (already declared once, in `Engine/EngineTypes.hpp`, for the same reason `MenuOption.hpp` does) and never calls into it inline, so including it does not by itself pull in a live host's engine-facing dependencies. It is not SDK-free on its own account, though: `Action` and `EffectDescriptor` carry an `ActionContext` that holds `Controller` by value, which needs `Pawn`/`Entity`/`Field<T, ...>` complete - true of `MenuBuilder` since before this phase, because a context row has always taken a `const Action&`. Only `MenuRenderer` (the plain `MenuView`/`MenuOption` → HTML step) is SDK-free; it is exercised by VoltMod's own SDK-free test suite (`tests/Menu/Html/MenuRenderTests.cpp`). The context-row bodies that call into the host - `Row`, `StateToggle`, `Presets`, `Effect`, `EffectPicker` - live in `MenuBuilderRows.cpp`, which includes `MenuHost.hpp` itself.
