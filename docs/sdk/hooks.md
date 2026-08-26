# Movement, teleports, and server commands {#sdk_hooks_guide}

[TOC]

## Movement

@ref VoltMod::Movement is a manual vtable hook on
`CCSPlayer_MovementServices::RunCommand`, the per-tick movement entry point. It exposes five
events; `Pre` and `Post` bracket one player's movement processing, which makes them the right
place for per-player state flips (see @ref VoltMod::ConVarStorage "ConVarStorage").

Subscribing is what installs the hook. The first subscription across all five events binds the
class vtable; dropping the last one unbinds it. There is no `Install()` to call.

```cpp
// Every `+=` returns a [[nodiscard]] Subscription. Keep it as a member next to whatever the
// handler captures, and the registration - and eventually the hook - goes away with that state.
_pre  = runtime.MovementHook.Pre  += [this](int slot) { /* before movement runs */ };
_post = runtime.MovementHook.Post += [this](int slot) { /* after it ran: restore */ };
```

Hook contracts:

- The hook is a SourceHook **DVP hook**: it binds the `CCSPlayer_MovementServices` class vtable, which is found in `server.dll`/`libserver.so` by the same RTTI (Windows) / ELF symbol (Linux) lookup @ref VoltMod::ClientCvars uses for `CServerSideClient`. No instance is involved, so subscribing from `OnLoad` with no player connected works, and the handlers then fire for every player, including ones who connect afterwards.
- When gamedata cannot resolve the hook, the subscription is **refused**: `+=` hands back an empty `Subscription`, the framework logs why, and `Installed()` stays false. A silent never-firing handler is the failure this replaces.
- The owning slot is resolved for you (`-1` when unresolved, e.g. an instance mid-destruction).
- Removal is by hook id. SourceHook resolves the id from what it recorded at add time and never dereferences the hooked object, so unsubscribing is safe after a map change has already destroyed every pawn.
- Two things **drift with CS2 updates** and are resolved together at install time: the vtable index, which lives in gamedata as `"RunCommand"`, and the class name. A wrong index calls an unrelated vfunc and crashes; a wrong class name resolves to nothing (or to another class's table) and the hook silently never fires - when a pawn happens to be live, its vtable is compared against the resolved one and a mismatch warns. Re-verify both (against SwiftlyS2/CS2Fixes gamedata) after every game update.

### Cmd events: reading the usercmd

`PreCmd`/`PostCmd` additionally hand you a @ref VoltMod::UserCmdView: the command's viewangles, held/changed button masks, raw mouse deltas, and per-subtick pitch/yaw deltas, decoded once per RunCommand from the `CSGOUserCmdPB` payload:

```cpp
_preCmd = runtime.MovementHook.PreCmd += [](int slot, const VoltMod::UserCmdView& cmd) {
    if (!cmd.Valid)
        return;  // null usercmd or missing gamedata offset
    // cmd.ViewYaw, cmd.MouseDx, cmd.ButtonsHeld, cmd.SubtickMoves[0].YawDelta, ...
};
```

The decode happens only while `PreCmd`, `PostCmd` or `FilterCmd` has a handler; plain `Pre`/`Post` stay free of it. The payload's byte offset inside the `CUserCmd` wrapper lives in gamedata as `"UserCmdPB"` (cross-checked against CS2Fixes and SwiftlyS2) and, like the vtable index, **must be re-verified after CS2 updates**. A missing offset degrades to `Valid=false` views rather than crashing, but a *stale* one reads garbage.

Important fields:

- `CommandNumber` is the client's own command counter. It is read from the `int32` the `CUserCmd` wrapper carries next to its payload (gamedata `"UserCmdNumber"`, 8 bytes in, which is why the payload itself starts at 16), because the protobuf's `legacy_command_number` stays 0 on a live client and only serves as the fallback when that offset is missing. Consecutive commands differ by exactly 1, so a gap means commands were lost, reordered, or synthesized. It is the cheap integrity check on the command stream itself.
- `HasViewAngles` says whether the command carried viewangles at all. When it is false the pitch/yaw/roll fields hold defaults, not a reading, so a `(0,0,0)` aim there is an absence rather than a measurement.
- `ViewRoll` is the third viewangles component (`viewangles.z`), decoded alongside pitch and yaw. Only pitch and yaw are driven by the player's mouse, so roll is the axis that carries whatever the client put there.

### Input history and the cap

`UserCmdView` also carries the per-shot input-history entries in `InputHistorySamples[k]` (`HasViewAngles`, `ViewPitch`/`ViewYaw`, `TargetEntIndex`), addressed by `Attack1StartHistoryIndex`/`Attack2StartHistoryIndex`. The shot's `view_angles` is the direction the bullet was actually fired along, which a cheat can diverge from the visible `ViewYaw`/`ViewPitch`, and that divergence is a silent-aim signature.

The decode keeps at most `MaxInputHistory` (16) entries, but the attack indexes address the client's *full* `input_history` list, so the two can disagree. `InputHistoryTotalCount` is what the client sent and `InputHistorySampleCount` is what survived the cap; a greater total means the tail was dropped. Never clamp an out-of-range attack index back into the array; that silently reads a different shot's angles. Use the accessors:

```cpp
const int index = cmd.Attack1StartHistoryIndex;
if (const auto* shot = cmd.SampleAt(index))
    Compare(shot->ViewYaw, cmd.ViewYaw);            // the entry is present
else if (index >= cmd.InputHistoryTotalCount)
    /* the client named an entry it never sent: a malformed command */;
else if (index >= 0)
    /* a shot happened but its angles were capped away, so no verdict */;
// otherwise the index is -1: no attack started this command
```

`SampleAt` returns `nullptr` for both the negative and the capped-away index, so compare the index against `InputHistoryTotalCount` to separate "never sent" from "capped away" from "no shot".

### FilterCmd: editing the decoded usercmd

`FilterCmd` hands you a **mutable** `UserCmdView&`. Filters run once, after the decode and before every `Pre`/`PreCmd`/`PostCmd` handler, so whatever a filter writes is what `InputHistory` and every cmd handler then observe:

```cpp
_filter = runtime.MovementHook.FilterCmd += [](int slot, VoltMod::UserCmdView& cmd) {
    cmd.ViewYaw += 90.0f;  // every downstream reader now sees the rotated view
};
```

The edit touches only the decoded snapshot; the underlying `CUserCmd` the engine processes is untouched (the hook still returns `MRES_IGNORED`), so the game is unaffected. This is for test/diagnostic input synthesis (feeding a detector a fabricated command), not for changing gameplay; leave it unused in normal operation.

### InputHistory: lookback over recent usercmds

@ref VoltMod::InputHistory (`runtime.InputHistory`) is an opt-in per-slot ring buffer of the decoded views, for plugins that need "what did this player's aim do over the last N ticks" (anti-cheat, movement analytics) without wiring their own buffers:

```cpp
runtime.InputHistory.Enable(128);                    // keep ~2s at 64 tick
int n = runtime.InputHistory.Count(slot);
const auto& newest = runtime.InputHistory.At(slot, 0);  // At(slot, ago)
```

`Enable` is the arming call because this is a query service with no event of its own - the depth
it needs has nowhere else to come from. It subscribes to `Movement::PreCmd` itself, so the
movement hook is installed for you.

History for a slot resets automatically when its player joins or leaves (via `runtime.Slots.Changed`, which is also the backing feed for the generic @ref VoltMod::PerSlot container).

## Teleport

@ref VoltMod::Teleport (`runtime.Teleports`) raises `Teleported(slot)` whenever a player pawn is moved by `CBaseEntity::Teleport`. It exists because a teleport breaks continuity: origin and view angles jump discontinuously, so anything measuring motion across ticks (speed, aim deltas, distance travelled) reads the frame after a teleport as impossible. Discount that window instead of explaining it away.

The hook is all the service owns - it keeps no history. How long the window lasts, and in which clock, is the consumer's question, so the consumer keeps the stamps:

```cpp
// Subscribing is what arms the per-pawn hook. PerSlot clears a stamp when the seat changes hands.
_lastTeleport.BindReset(runtime.Slots);
_teleports = runtime.Teleports.Teleported += [this](int slot) {
    if (VoltMod::IsValidSlot(slot))
        _lastTeleport[slot] = _rt.Clock.Time();
};

if (!JustTeleported(slot))       // your own window, against your own clock
    EvaluateAim(slot);
```

Semantics worth knowing:

- The first subscription hooks the `"Teleport"` vtable index on every *live* pawn; the binding is per pawn, not per class, so exactly one handler call happens per teleport however many pawns are bound. A missing gamedata offset refuses the subscription (empty `Subscription`, `Enabled()` stays false) rather than tracking nothing quietly.
- The slot argument is `-1` when the teleported pawn belongs to no player, so guard before indexing.
- Respawning hands the player a brand-new pawn object, which makes the previous binding stale. The tracker re-binds from its own `PlayerSpawn` handler. Since a spawn also moves the player, **a spawn raises the event too**. If you only care about mid-life teleports, filter spawns yourself.
- The framework's `StartupServer` hook drops every binding at map start, before the previous map's pawn addresses are recycled. If you stamp @ref VoltMod::Clock "runtime.Clock" times, remember that clock restarts with the map: a stamp ahead of the current time belongs to the previous one.

## Damage

@ref VoltMod::Damage (`runtime.Damage`) is a manual vtable hook on
`CCSPlayerPawn::OnTakeDamage_Alive` - every point of damage a living player takes. Handlers see
who hit whom, where, and for how much.

```cpp
_damage = runtime.Damage.Hit += [this](const VoltMod::DamageView& view) {
    if (view.AttackerSlot < 0)                   // world damage: fall, fire, the bomb
        return;
    if (view.Hitbox == VoltMod::HitGroup::Head)
        RecordHeadshot(view.AttackerSlot);
};
```

Contracts worth knowing:

- **Observation only**, hence the `const` view. By the time handlers run the engine has already
  turned `CTakeDamageInfo` into the result it applies, so nothing done here changes what the
  victim loses - measured in game, `MRES_SUPERCEDE`, writes to both structs, and writing the
  victim's health from the handler are all ignored. To alter damage, change the game's own rules:
  `mp_damage_headshot_only` and the `mp_damage_scale_*` multipliers do work, and are what the
  admin-system fun toggles drive.
- Like Movement this is a **DVP hook** on the class vtable installed by the first subscription, so
  subscribing from `OnLoad` with no player connected works and covers every pawn from then on.
  Dropping the last subscription removes it, and a gamedata failure refuses the subscription
  outright (`Installed()` stays false).
- `Hitbox` comes from the damage trace, not `m_iHitGroupId` (which reads `-1` for bullets). It is
  `Invalid` for damage with no trace behind it, which is how world damage is told apart. The
  hitgroup on @ref VoltMod::PlayerHurt comes from the event instead, and reads `Generic` rather
  than `Invalid` when absent.
- Both slots are resolved for you, `-1` when the pawn is not a player's. The lookup is
  constant-time, which matters: this fires per bullet, per pellet, and per fire or HE tick against
  every victim in radius.
- The vtable index **and** every field offset are gamedata-maintained (`OnTakeDamageAlive`,
  `TakeDamageInfo*`, `GameTraceHitbox`, `HitboxGroupId`), so a CS2 layout
  change is a gamedata edit. Anything that fails to resolve refuses the subscription rather than
  acting on the wrong bytes.

## ServerCommand

@ref VoltMod::ServerCommand is a RAII tier1 `ConCommand`: registered on construction, unregistered on destruction, with a `std::function` handler that runs on the game thread.

```cpp
class MyManager
{
    std::optional<VoltMod::ServerCommand> _cmd;

    void Initialize()
    {
        _cmd.emplace("myplugin_do", "Do the thing: myplugin_do <steamid64>",
                     [this](const CCommand& args) { /* args.ArgC(), args.Arg(1), ... */ });
    }
};
```

Use server commands for console, RCON, cfg files, and loose automation. For a
typed plugin-to-plugin contract, publish a versioned interface through
`runtime.Exchange` and query it at the point of use. Plugins are separate
modules, so never transfer ownership or allow exceptions across that boundary.

Commands remain useful as a compatibility fallback. A caller can invoke one
with `runtime.ConVars.ExecuteServerCommand("myplugin_do 765...")`; if the
provider is absent, the engine reports an unknown command.

Construct only while the plugin is loaded (ICvar must be live), typically as a manager member, so unload unregisters it automatically.
