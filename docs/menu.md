# Menus {#menus_guide}

[TOC]

VoltMod menus use WASD input and center HTML. Each row is a typed
@ref VoltMod::Menu::MenuOption. Build menus with
@ref VoltMod::Menu::MenuBuilder; context rows and the
@ref VoltMod::Menu::Flow wizard provide the common admin-panel behavior.

Players navigate with:

| Key | Action |
| --- | --- |
| **W** / **S** | Move the cursor up / down |
| **E** | Activate the highlighted row (button, toggle flip, choice commit, input prompt, ...) |
| **A** / **D** | Adjust a value row (Toggle / Choice / Selector / Slider); otherwise page through a paginated menu |
| **R** | Close (root) / back (submenu). Cancels an active chat-input capture. |

## Building a menu

```cpp
#include <VoltMod/Menu/MenuBuilder.hpp>

using namespace VoltMod::Menu;

auto menu = MenuBuilder("Admin Panel")
    .AddButton("Kick Player", [](int slot) { /* ... */ })
    .AddButton("Disabled",    [](int slot) {}, /*enabled=*/false)
    .OnClose([](int slot) { /* cleanup */ })
    .Build();

runtime.Menus.OpenMenu(playerSlot, menu);
```

## Context rows

For rows that act on an admin/target pair, bind a @ref VoltMod::Menu::MenuContext once. It carries the runtime as its first member; every context row then derives its label (a translation key in the admin's language), its enabled state (permission and immunity via `runtime.Policy`, so a row the admin cannot use does not appear), and its dispatch pair from the context:

```cpp
MenuBuilder(title)
    .WithContext({.Rt = &runtime, .Admin = adminSlot, .Target = targetSlot, .Effects = &app.Effects})
    .AddActionRow("action.kill", Actions::Kill)                                    // runs an Action
    .AddStateToggleRow("action.freeze", InMoveType(MoveType::None), Actions::Freeze)  // live on/off state
    .AddPresetChoiceRow("action.health", "HP", HealthPresets, Actions::SetHealth)  // A/D cycles, E applies
    .AddEffectToggleRow(Effects::Ghost)          // data-defined effect (EffectDescriptor)
    .AddEffectPickerRow(Effects::Model)          // submenu over the effect's Choices
    .Build();
```

A context left without `.Rt` is inert: `Allowed` denies, so every context row renders disabled. `AddStateToggleRow` re-reads its predicate every redraw, so the same row shows "Freeze"/"Unfreeze" reality and doubles as the undo control. The pawn predicates (`InMoveType`, `HasPawnFlag`) live in `Entities/PawnPredicates.hpp`. Effect rows read on/off labels from the reserved keys `effectState.on` / `effectState.off`; the descriptors themselves are covered in @ref players_guide.

## Flow: multistep wizards

@ref VoltMod::Menu::Flow carries a state struct through steps such as "pick
duration, pick reason, confirm, execute". It re-runs validation before each step
and before finishing, so a departed target or revoked permission aborts cleanly
instead of applying half the action.

```cpp
VoltMod::Flow<PendingPunishment>::Create(runtime.Menus, std::move(pending))
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
- `Create` takes the @ref VoltMod::Menu::MenuManager the flow opens and closes its steps through, so the flow needs no other service.
- The `OnValidate` result is a translation key. On failure the flow calls `MenuManager::CloseAllWithReply`, which replies through `runtime.Policy.Reply` and closes the menus.
- A confirm-only flow (skip straight to `WithConfirm`) is the natural shape for "quick" variants of a wizard.
- Lifetime is automatic: menu rows hold the only owning references, so the flow lives exactly as long as one of its menus is on screen. There is no manager to hold and no cleanup to write.
- `AddStep(build, applies)` is the escape hatch for a fully custom step: build any menu, mutate `flow.State()`, and call `flow.Advance(slot)`.

## Option types

Every builder method appends a typed row. Use
`AddOption(std::shared_ptr<MenuOption>)` for a custom subclass.

- `AddText(label)` is a non-selectable heading or divider; the cursor skips it.
- `AddButton(label, onActivate, enabled = true)` is a plain action row. `AddDynamicButton(getLabel, ...)` recomputes the label every frame.
- `AddToggle(title, onLabel, offLabel, getState, onToggle, enabled = true)` renders `"title: ON|OFF"`; E and A/D both flip. State lives wherever you keep it, so pass a getter.
- `AddChoice<T>(title, choices, onCommit, enabled = true, initialIndex = 0)`: A/D cycles the `{label, value}` list and E commits the current value. The option owns its index, so ephemeral pick-one rows need no external state:

```cpp
.AddChoice<int>("HP", {{"1 HP", 1}, {"100 HP", 100}, {"999 HP", 999}},
    [admin, target](int slot, const int& hp) {
        Actions::DoSetHealth(admin, target, hp);
        runtime.Menus.CloseAllMenus(slot);
    })
```

  The getter/setter overload (`AddChoice<T>(title, choices, getIndex, setIndex, onCommit, enabled)`) remains for state that lives outside the menu. With no `onCommit`, E advances like D, which suits pick-a-value rows another part of the menu reads live.

- `AddSelector<T>(title, values, formatter, ...)` is Choice for value types without their own label (seconds → `"5m"`, enum → translation).
- `AddSlider(title, min, max, step, getValue, setValue, enabled = true)`: A/D adjusts in steps, clamped, and renders a unicode bar.
- `AddProgressBar(title, getValue, max)` is a read-only bar the cursor skips.
- `AddInput(title, prompt, get, set, maxLength = 64, enabled = true)`: E pauses the menu and routes the player's next chat line into `set`; return `false` to re-prompt, `true` to accept. R cancels. Backed by @ref VoltMod::Hooks::ChatInput, so your chat hook must call `runtime.ChatInput.TryConsume` first (see @ref sdk_messaging_guide).
- `AddSubmenu(label, factory, enabled = true)` runs the factory lazily on E and pushes the returned menu onto the stack; R pops back.

## Pagination

More than `ItemsPerPage` rows (5 by default) paginates automatically, with a `(2/3)` indicator and an `[A/D] Page` footer hint. A/D is item-aware: on a value row it adjusts the value; highlight a Button/Submenu row to page. Disabled and non-selectable rows are skipped by the cursor.

## Custom layout

```cpp
MenuBuilder("Custom")
    .WithHeader([] { return "<b>Server Admin</b><br><i>v1.0</i>"; })
    .WithFooter([] { return "<font color='gray'>WASD to navigate</font>"; })
```

## Lifetime and input

@ref VoltMod::Menu::MenuManager keeps a per-player stack, reads button state every frame (via a self-registered scheduler pump), debounces input (200 ms), and clears a player's stack on disconnect. `runtime.Menus.SetFreezePlayer(true)` freezes players while a menu is open so WASD doesn't also move them. During a chat-input capture only R is honored, so the cursor doesn't drift while the player types.

The freeze is a global switch, but a single session can opt out: `OpenMenu(slot, menu, {.FreezeMovement = false})`. That suits menus ordinary players reach mid-round, where being held still is worse than the stray movement the freeze prevents. @ref VoltMod::Menu::MenuSessionOptions applies to the call that opens the stack; submenus and Flow steps pushed onto a live session inherit it, so an unfrozen session stays unfrozen for its whole flow.

## Presets

`<VoltMod/Menu/MenuPresets.hpp>` ships content-agnostic building blocks; every human-facing string is a parameter, and each preset takes the one service it needs as its first argument:

```cpp
using namespace VoltMod::Menu;

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
auto confirm = BuildConfirmDialog(runtime.Menus, {
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

Most consumers only need `MenuBuilder.hpp` (which pulls in every option type) plus `Flow.hpp` or `MenuPresets.hpp`. The per-option headers under `Menu/Options/` matter only when constructing an option manually for `AddOption` or subclassing `MenuOption` (override `GetLabel`, `OnActivate(int slot, MenuManager& menus)`, and optionally `OnHorizontal`). `OnActivate` receives the manager rendering the row, so a custom option can push a submenu (`menus.OpenMenu`) or start a chat prompt (`menus.BeginInput`) without holding the runtime.
