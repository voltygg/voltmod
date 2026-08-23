# Movement, Teleports & Server Commands {#sdk_hooks_guide}

[TOC]

## MovementHook

@ref CS2Kit::Sdk::MovementHook is a manual vtable hook on `CPlayer_MovementServices::RunCommand` - the per-tick, per-player movement entry point. Pre/post callbacks bracket exactly one player's movement processing, which makes the pair the right place for per-player state flips (see @ref CS2Kit::Sdk::RawConVar "RawConVar").

The service is a dormant `Runtime` member: it costs nothing until a plugin calls `Install()`, and it removes its hook on destruction.

```cpp
// Callbacks can be registered up front; they fire only once the hook is installed.
// Every Listen* returns a [[nodiscard]] Subscription - keep it as a member next to
// whatever the callback captures, and the registration goes away with that state.
_pre  = runtime.MovementHook.ListenPre([this](int slot) { /* before movement runs */ });
_post = runtime.MovementHook.ListenPost([this](int slot) { /* after it ran - restore */ });

// Install needs a live movement-services instance (a spawned pawn), so call it lazily
// and treat false as "retry later" - a PlayerSpawn listener is the natural place:
_spawn = runtime.Events.Listen<Events::PlayerSpawn>([&runtime](const Events::PlayerSpawn&) {
    runtime.MovementHook.Install();   // no-op once installed
});
```

Details worth knowing:

- The hook is a SourceHook **VP hook**: it binds to the shared `CPlayer_MovementServices` vtable, not to the instance passed in. `Install()` only needs some live instance to locate the table; the callbacks then fire for every player, including ones who spawn afterwards. (A plain manual hook would fire only for the one instance it was registered on.)
- The owning slot is resolved for you (`-1` when unresolved, e.g. an instance mid-destruction).
- Removal is by hook id. SourceHook resolves the id from what it recorded at add time and never dereferences the hooked object, so `Remove()` is safe after a map change has already destroyed every pawn.
- The vtable index lives in gamedata as `"RunCommand"` and **drifts with CS2 updates** - a wrong index calls an unrelated vfunc and crashes. Re-verify it (against SwiftlyS2/CS2Fixes gamedata) after every game update.

### Cmd listeners: reading the usercmd

`ListenPreCmd`/`ListenPostCmd` additionally hand you a @ref CS2Kit::Sdk::UserCmdView - the command's viewangles, held/changed button masks, raw mouse deltas, and per-subtick pitch/yaw deltas, decoded once per RunCommand from the `CSGOUserCmdPB` payload:

```cpp
_preCmd = runtime.MovementHook.ListenPreCmd([](int slot, const CS2Kit::UserCmdView& cmd) {
    if (!cmd.Valid)
        return;  // null usercmd or missing gamedata offset
    // cmd.ViewYaw, cmd.MouseDx, cmd.ButtonsHeld, cmd.SubtickMoves[0].YawDelta, ...
});
```

The decode happens only while at least one cmd (or filter) listener is registered; plain `ListenPre`/`ListenPost` stay free of it. The payload's byte offset inside the `CUserCmd` wrapper lives in gamedata as `"UserCmdPB"` (cross-checked against CS2Fixes and SwiftlyS2) and, like the vtable index, **must be re-verified after CS2 updates** - a missing offset degrades to `Valid=false` views rather than crashing, but a *stale* one reads garbage.

Three fields are worth calling out beyond the obvious aim/button ones:

- `CommandNumber` is the client's own command counter. It is read from the `int32` the `CUserCmd` wrapper carries next to its payload (gamedata `"UserCmdNumber"`, 8 bytes in - which is why the payload itself starts at 16), because the protobuf's `legacy_command_number` stays 0 on a live client and only serves as the fallback when that offset is missing. Consecutive commands differ by exactly 1, so a gap means commands were lost, reordered, or synthesized - it is the cheap integrity check on the command stream itself.
- `HasViewAngles` says whether the command carried viewangles at all. When it is false the pitch/yaw/roll fields hold defaults, not a reading, so a `(0,0,0)` aim there is an absence rather than a measurement.
- `ViewRoll` is the third viewangles component (`viewangles.z`), decoded alongside pitch and yaw. Only pitch and yaw are driven by the player's mouse, so roll is the axis that carries whatever the client put there.

### Input history and the cap

`UserCmdView` also carries the per-shot input-history entries in `InputHistorySamples[k]` (`HasViewAngles`, `ViewPitch`/`ViewYaw`, `TargetEntIndex`), addressed by `Attack1StartHistoryIndex`/`Attack2StartHistoryIndex`. The shot's `view_angles` is the direction the bullet was actually fired along, which a cheat can diverge from the visible `ViewYaw`/`ViewPitch` - that divergence is a silent-aim signature.

The decode keeps at most `MaxInputHistory` (16) entries, but the attack indexes address the client's *full* `input_history` list, so the two can disagree. `InputHistoryTotalCount` is what the client sent and `InputHistorySampleCount` is what survived the cap; a greater total means the tail was dropped. Never clamp an out-of-range attack index back into the array - that silently reads a different shot's angles. Use the accessors:

```cpp
const int index = cmd.Attack1StartHistoryIndex;
if (const auto* shot = cmd.SampleAt(index))
    Compare(shot->ViewYaw, cmd.ViewYaw);            // the entry is present
else if (index >= cmd.InputHistoryTotalCount)
    /* the client named an entry it never sent - a malformed command */;
else if (index >= 0)
    /* a shot happened but its angles were capped away - no verdict */;
// otherwise the index is -1: no attack started this command
```

`SampleAt` returns `nullptr` for both the negative and the capped-away index, so compare the index against `InputHistoryTotalCount` to separate "never sent" from "capped away" from "no shot".

### Filter listeners: editing the decoded usercmd

`ListenFilterCmd` hands you a **mutable** `UserCmdView&`. Filters run once, after the decode and before every pre/preCmd/postCmd listener, so whatever a filter writes is what `InputHistory` and every cmd listener then observe:

```cpp
_filter = runtime.MovementHook.ListenFilterCmd([](int slot, CS2Kit::UserCmdView& cmd) {
    cmd.ViewYaw += 90.0f;  // every downstream reader now sees the rotated view
});
```

The edit touches only the decoded snapshot - the underlying `CUserCmd` the engine processes is untouched (the hook still returns `MRES_IGNORED`), so the game is unaffected. This is for test/diagnostic input synthesis (feeding a detector a fabricated command), not for changing gameplay; leave it unused in normal operation.

### InputHistoryService: lookback over recent usercmds

@ref CS2Kit::Sdk::InputHistoryService (`runtime.InputHistory`) is an opt-in per-slot ring buffer of the decoded views - for plugins that need "what did this player's aim do over the last N ticks" (anti-cheat, movement analytics) without wiring their own buffers:

```cpp
runtime.InputHistory.Enable(128);                    // keep ~2s at 64 tick
int n = runtime.InputHistory.Count(slot);
const auto& newest = runtime.InputHistory.At(slot, 0);  // At(slot, ago)
```

History for a slot resets automatically when its player joins or leaves (via @ref CS2Kit::Players::PlayerManager::ListenSlotChange, which is also the backing feed for the generic @ref CS2Kit::Players::PerSlot container). The MovementHook must still be installed for samples to flow.

## TeleportTracker

@ref CS2Kit::Sdk::TeleportTracker (`runtime.Teleports`) records when each player's pawn was last moved by `CBaseEntity::Teleport`. It exists because a teleport breaks continuity: origin and view angles jump discontinuously, so anything measuring motion across ticks - speed, aim deltas, distance travelled - reads the frame after a teleport as impossible. Discount that window instead of explaining it away.

```cpp
runtime.Teleports.Enable();                            // dormant until this call

if (!runtime.Teleports.JustTeleported(slot, 0.5f))     // seconds of server time
    EvaluateAim(slot);
```

Semantics worth knowing:

- `Enable()` hooks the `"Teleport"` vtable index on every *live* pawn and returns false when that gamedata offset is missing. The binding is per pawn, not per class, so exactly one callback fires per teleport however many pawns are bound.
- Respawning hands the player a brand-new pawn object, which makes the previous binding stale. The tracker re-binds from its own `PlayerSpawn` listener - and since a spawn also moves the player, **a spawn counts as a teleport** and gets stamped. If you only care about mid-life teleports, filter spawns yourself.
- Stamps are @ref CS2Kit::Sdk::ServerTime "ServerTime()" values, so they are meaningless across a map change. The kit's `StartupServer` hook drops every binding and stamp at map start, and a slot with no stamp reads as never teleported.

## ServerCommand

@ref CS2Kit::Sdk::ServerCommand is a RAII tier1 `ConCommand`: registered on construction, unregistered on destruction, with a `std::function` handler that runs on the game thread.

```cpp
class MyManager
{
    std::optional<CS2Kit::ServerCommand> _cmd;

    void Initialize()
    {
        _cmd.emplace("myplugin_do", "Do the thing: myplugin_do <steamid64>",
                     [this](const CCommand& args) { /* args.ArgC(), args.Arg(1), ... */ });
    }
};
```

Beyond console/cfg use, this is the standard **cross-plugin surface**: plugins are isolated modules that cannot share managers, so a feature one plugin should drive in another is exposed as a server command and invoked via `runtime.ConVars.ExecuteServerCommand("myplugin_do 765...")`. When the providing plugin is absent the engine just logs "Unknown command" - graceful degradation for free. (This is how admin-system's Bunnyhop effect drives the bhop plugin's `bhop_player` command.)

Construct only while the plugin is loaded (ICvar must be live) - typically as a manager member, so unload unregisters it automatically.
