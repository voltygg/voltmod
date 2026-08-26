# Movement, teleports, and server commands {#sdk_hooks_guide}

[TOC]

## MovementHook

@ref VoltMod::Sdk::MovementHook is a manual vtable hook on
`CCSPlayer_MovementServices::RunCommand`, the per-tick movement entry point.
Pre/post callbacks bracket one player's movement processing, which makes them
the right place for per-player state flips (see
@ref VoltMod::Sdk::ConVarStorage "ConVarStorage").

The service stays dormant until a plugin calls `Install()` and removes its hook
on destruction.

```cpp
// Callbacks can be registered up front; they fire only once the hook is installed.
// Every Listen* returns a [[nodiscard]] Subscription. Keep it as a member next to
// whatever the callback captures, and the registration goes away with that state.
_pre  = runtime.MovementHook.ListenPre([this](int slot) { /* before movement runs */ });
_post = runtime.MovementHook.ListenPost([this](int slot) { /* after it ran: restore */ });

// Install binds the class vtable, so an empty server is fine: call it once from OnLoad.
runtime.MovementHook.Install();   // no-op once installed
```

Hook contracts:

- The hook is a SourceHook **DVP hook**: it binds the `CCSPlayer_MovementServices` class vtable, which is found in `server.dll`/`libserver.so` by the same RTTI (Windows) / ELF symbol (Linux) lookup @ref VoltMod::Sdk::ClientCvarService uses for `CServerSideClient`. No instance is involved, so `Install()` succeeds with no player connected and the callbacks then fire for every player, including ones who connect afterwards.
- The owning slot is resolved for you (`-1` when unresolved, e.g. an instance mid-destruction).
- Removal is by hook id. SourceHook resolves the id from what it recorded at add time and never dereferences the hooked object, so `Remove()` is safe after a map change has already destroyed every pawn.
- Two things **drift with CS2 updates** and are resolved together at install time: the vtable index, which lives in gamedata as `"RunCommand"`, and the class name. A wrong index calls an unrelated vfunc and crashes; a wrong class name resolves to nothing (or to another class's table) and the hook silently never fires - when a pawn happens to be live, `Install()` compares its vtable against the resolved one and warns on a mismatch. Re-verify both (against SwiftlyS2/CS2Fixes gamedata) after every game update.

### Cmd listeners: reading the usercmd

`ListenPreCmd`/`ListenPostCmd` additionally hand you a @ref VoltMod::Sdk::UserCmdView: the command's viewangles, held/changed button masks, raw mouse deltas, and per-subtick pitch/yaw deltas, decoded once per RunCommand from the `CSGOUserCmdPB` payload:

```cpp
_preCmd = runtime.MovementHook.ListenPreCmd([](int slot, const VoltMod::UserCmdView& cmd) {
    if (!cmd.Valid)
        return;  // null usercmd or missing gamedata offset
    // cmd.ViewYaw, cmd.MouseDx, cmd.ButtonsHeld, cmd.SubtickMoves[0].YawDelta, ...
});
```

The decode happens only while at least one cmd (or filter) listener is registered; plain `ListenPre`/`ListenPost` stay free of it. The payload's byte offset inside the `CUserCmd` wrapper lives in gamedata as `"UserCmdPB"` (cross-checked against CS2Fixes and SwiftlyS2) and, like the vtable index, **must be re-verified after CS2 updates**. A missing offset degrades to `Valid=false` views rather than crashing, but a *stale* one reads garbage.

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

### Filter listeners: editing the decoded usercmd

`ListenFilterCmd` hands you a **mutable** `UserCmdView&`. Filters run once, after the decode and before every pre/preCmd/postCmd listener, so whatever a filter writes is what `InputHistory` and every cmd listener then observe:

```cpp
_filter = runtime.MovementHook.ListenFilterCmd([](int slot, VoltMod::UserCmdView& cmd) {
    cmd.ViewYaw += 90.0f;  // every downstream reader now sees the rotated view
});
```

The edit touches only the decoded snapshot; the underlying `CUserCmd` the engine processes is untouched (the hook still returns `MRES_IGNORED`), so the game is unaffected. This is for test/diagnostic input synthesis (feeding a detector a fabricated command), not for changing gameplay; leave it unused in normal operation.

### InputHistoryService: lookback over recent usercmds

@ref VoltMod::Sdk::InputHistoryService (`runtime.InputHistory`) is an opt-in per-slot ring buffer of the decoded views, for plugins that need "what did this player's aim do over the last N ticks" (anti-cheat, movement analytics) without wiring their own buffers:

```cpp
runtime.InputHistory.Enable(128);                    // keep ~2s at 64 tick
int n = runtime.InputHistory.Count(slot);
const auto& newest = runtime.InputHistory.At(slot, 0);  // At(slot, ago)
```

History for a slot resets automatically when its player joins or leaves (via @ref VoltMod::Players::PlayerManager::ListenSlotChange, which is also the backing feed for the generic @ref VoltMod::Players::PerSlot container). The MovementHook must still be installed for samples to flow.

## TeleportTracker

@ref VoltMod::Sdk::TeleportTracker (`runtime.Teleports`) records when each player's pawn was last moved by `CBaseEntity::Teleport`. It exists because a teleport breaks continuity: origin and view angles jump discontinuously, so anything measuring motion across ticks (speed, aim deltas, distance travelled) reads the frame after a teleport as impossible. Discount that window instead of explaining it away.

```cpp
runtime.Teleports.Enable();                            // dormant until this call

if (!runtime.Teleports.JustTeleported(slot, 0.5f))     // seconds of server time
    EvaluateAim(slot);
```

Semantics worth knowing:

- `Enable()` hooks the `"Teleport"` vtable index on every *live* pawn and returns false when that gamedata offset is missing. The binding is per pawn, not per class, so exactly one callback fires per teleport however many pawns are bound.
- Respawning hands the player a brand-new pawn object, which makes the previous binding stale. The tracker re-binds from its own `PlayerSpawn` listener. Since a spawn also moves the player, **a spawn counts as a teleport** and gets stamped. If you only care about mid-life teleports, filter spawns yourself.
- Stamps are @ref VoltMod::Sdk::ServerClock "runtime.Clock" times, so they are meaningless across a map change. The framework's `StartupServer` hook drops every binding and stamp at map start, and a slot with no stamp reads as never teleported.

## DamageHook

@ref VoltMod::Sdk::DamageHook (`runtime.Damage`) is a manual vtable hook on
`CCSPlayerPawn::OnTakeDamage_Alive` - every point of damage a living player takes. Listeners see
who hit whom, where, and for how much.

```cpp
runtime.Damage.Install();       // binds the class vtable; safe from OnLoad, no-op once installed

_damage = runtime.Damage.Listen([this](const VoltMod::DamageView& view) {
    if (view.AttackerSlot < 0)                   // world damage: fall, fire, the bomb
        return;
    if (view.Hitbox == VoltMod::HitGroup::Head)
        RecordHeadshot(view.AttackerSlot);
});
```

Contracts worth knowing:

- **Observation only**, hence the `const` view. By the time listeners run the engine has already
  turned `CTakeDamageInfo` into the result it applies, so nothing done here changes what the
  victim loses - measured in game, `MRES_SUPERCEDE`, writes to both structs, and writing the
  victim's health from the listener are all ignored. To alter damage, change the game's own rules:
  `mp_damage_headshot_only` and the `mp_damage_scale_*` multipliers do work, and are what the
  admin-system fun toggles drive.
- Like MovementHook this is a **DVP hook** on the class vtable, so `Install()` works from `OnLoad`
  with no player connected and covers every pawn from then on.
- `Hitbox` comes from the damage trace, not `m_iHitGroupId` (which reads `-1` for bullets). It is
  `Invalid` for damage with no trace behind it, which is how world damage is told apart.
- Both slots are resolved for you, `-1` when the pawn is not a player's. The lookup is
  constant-time, which matters: this fires per bullet, per pellet, and per fire or HE tick against
  every victim in radius.
- An installed hook with no listeners returns immediately, so arming it up front is free.
- The vtable index **and** every field offset are gamedata-maintained (`OnTakeDamageAlive`,
  `TakeDamageInfo*`, `GameTraceHitbox`, `HitboxGroupId`), so a CS2 layout
  change is a gamedata edit. Anything that fails to resolve leaves the hook uninstalled and every
  listener silent rather than acting on the wrong bytes.

## ServerCommand

@ref VoltMod::Sdk::ServerCommand is a RAII tier1 `ConCommand`: registered on construction, unregistered on destruction, with a `std::function` handler that runs on the game thread.

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
