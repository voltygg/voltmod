# Movement, teleports, and server commands {#sdk_hooks_guide}

[TOC]

Every hook service here is **lazily installed**: subscribing to its event is what
binds the engine, and dropping the last subscription unbinds it again. None of
them has an `Install()`, `Enable()` or `Available()` of its own - whether the
gamedata behind one resolved is @ref VoltMod::Capabilities "runtime.Capabilities",
recorded once by `Runtime::Start` before any plugin's `OnLoad` runs. The last
section shows how to hook an engine vfunc the framework does not cover, with the
same two pieces the services use.

## Movement

@ref VoltMod::Movement is a manual vtable hook on
`CCSPlayer_MovementServices::RunCommand`, the per-tick movement entry point. It exposes five
events; `Pre` and `Post` bracket one player's movement processing, which makes them the right
place for per-player state flips (see @ref VoltMod::ConVar::RawScope "ConVar::RawScope").

Subscribing is what installs the hook. The first subscription across all five events binds the
class vtable; dropping the last one unbinds it. There is no `Install()` to call.

```cpp
// Every `+=` returns a [[nodiscard]] Subscription. Keep it as a member next to whatever the
// handler captures, and the registration - and eventually the hook - goes away with that state.
_pre  = runtime.Hooks.Movement.Pre  += [this](int slot) { /* before movement runs */ };
_post = runtime.Hooks.Movement.Post += [this](int slot) { /* after it ran: restore */ };
```

Hook contracts:

- The hook is a SourceHook **DVP hook**: it binds the `CCSPlayer_MovementServices` class vtable, which is found in `server.dll`/`libserver.so` by the same RTTI (Windows) / ELF symbol (Linux) lookup @ref VoltMod::ClientCvars uses for `CServerSideClient`. No instance is involved, so subscribing from `OnLoad` with no player connected works, and the handlers then fire for every player, including ones who connect afterwards.
- When gamedata cannot resolve the hook, the subscription is **refused**: `+=` hands back an empty `Subscription` and the framework logs why. A silent never-firing handler is the failure this replaces. Read `runtime.Capabilities.Has(Capability::Movement)` beforehand if you would rather not attempt it.
- The pre and post sides go in as a **pair or not at all**. A lone post would run against state the pre it brackets never established, so if SourceHook refuses either side nothing stays installed and the log names the side that failed.
- The owning slot is resolved for you (`-1` when unresolved, e.g. an instance mid-destruction).
- Removal is by hook id. SourceHook resolves the id from what it recorded at add time and never dereferences the hooked object, so unsubscribing is safe after a map change has already destroyed every pawn.
- Two things **drift with CS2 updates** and are resolved together at install time: the vtable index, which lives in gamedata as `"RunCommand"`, and the class name. A wrong index calls an unrelated vfunc and crashes; a wrong class name resolves to nothing (or to another class's table) and the hook silently never fires - when a pawn happens to be live, its vtable is compared against the resolved one and a mismatch warns. Re-verify both (against SwiftlyS2/CS2Fixes gamedata) after every game update.

### Cmd events: reading the usercmd

`PreCmd` additionally hands you a @ref VoltMod::UserCmdView: the command's viewangles, held/changed button masks, raw mouse deltas, and per-subtick pitch/yaw deltas, decoded once per RunCommand from the `CSGOUserCmdPB` payload:

```cpp
_preCmd = runtime.Hooks.Movement.PreCmd += [](int slot, const VoltMod::UserCmdView& cmd) {
    if (!cmd.Valid)
        return;  // null usercmd or missing gamedata offset
    // cmd.ViewYaw, cmd.MouseDx, cmd.ButtonsHeld, cmd.SubtickMoves[0].YawDelta, ...
};
```

The decode happens only while `PreCmd` or `FilterCmd` has a handler; plain `Pre`/`Post` stay free of it. The payload's byte offset inside the `CUserCmd` wrapper lives in gamedata as `"UserCmdPB"` (cross-checked against CS2Fixes and SwiftlyS2) and, like the vtable index, **must be re-verified after CS2 updates**. A missing offset degrades to `Valid=false` views rather than crashing, but a *stale* one reads garbage.

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

`FilterCmd` hands you a **mutable** `UserCmdView&`. Filters run once, after the decode and before every `Pre`/`PreCmd` handler, so whatever a filter writes is what every cmd handler then observes:

```cpp
_filter = runtime.Hooks.Movement.FilterCmd += [](int slot, VoltMod::UserCmdView& cmd) {
    cmd.ViewYaw += 90.0f;  // every downstream reader now sees the rotated view
};
```

The edit touches only the decoded snapshot; the underlying `CUserCmd` the engine processes is untouched (the hook still returns `MRES_IGNORED`), so the game is unaffected. This is for test/diagnostic input synthesis (feeding a detector a fabricated command), not for changing gameplay; leave it unused in normal operation.

## Teleport

@ref VoltMod::Teleport (`runtime.Hooks.Teleport`) raises `Teleported(slot)` whenever a player pawn is moved by `CBaseEntity::Teleport`. It exists because a teleport breaks continuity: origin and view angles jump discontinuously, so anything measuring motion across ticks (speed, aim deltas, distance travelled) reads the frame after a teleport as impossible. Discount that window instead of explaining it away.

The hook is all the service owns - it keeps no history. How long the window lasts, and in which clock, is the consumer's question, so the consumer keeps the stamps:

```cpp
// Subscribing is what arms the per-pawn hook. PerSlot clears a stamp when the seat changes hands.
_lastTeleport.BindReset(runtime.Slots);
_teleports = runtime.Hooks.Teleport.Teleported += [this](int slot) {
    if (VoltMod::IsValidSlot(slot))
        _lastTeleport[slot] = _rt.Clock.Time();
};

if (!JustTeleported(slot))       // your own window, against your own clock
    EvaluateAim(slot);
```

Semantics worth knowing:

- The first subscription hooks the `"Teleport"` vtable index on every *live* pawn; the binding is per pawn, not per class, so exactly one handler call happens per teleport however many pawns are bound. A missing gamedata offset refuses the subscription (an empty `Subscription`) rather than tracking nothing quietly; `Capability::Teleport` carries the reason.
- The slot argument is `-1` when the teleported pawn belongs to no player, so guard before indexing.
- Respawning hands the player a brand-new pawn object, which makes the previous binding stale. The tracker re-binds from its own `PlayerSpawn` handler. Since a spawn also moves the player, **a spawn raises the event too**. If you only care about mid-life teleports, filter spawns yourself.
- The framework's `StartupServer` hook drops every binding at map start, before the previous map's pawn addresses are recycled. If you stamp @ref VoltMod::Clock "runtime.Clock" times, remember that clock restarts with the map: a stamp ahead of the current time belongs to the previous one.

## Hooking a vfunc the framework does not cover

`<VoltMod/Unsafe/VtableHook.hpp>` is the same pair of pieces the four services
above are built from. `Unsafe` is an opt-in tier, not a compatibility layer: a
wrong vtable index calls an unrelated function and crashes the server, and
nothing here can check that for you.

Two pieces, because a hook has two halves that cannot live in the same place:

- `VOLTMOD_VHOOK<arity>(Name, Ret, params...)` (and `VOLTMOD_VHOOK<arity>_VOID`)
  at **namespace scope in the .cpp that owns the hook**. It expands to
  SourceHook's manual-hook declaration plus a `Name##Hook` traits type.
- @ref VoltMod::VtableHook "VtableHook", the installed hook as a value. It owns
  the SourceHook ids, and dropping it removes them.

```cpp
#include <VoltMod/Engine/MetamodGlobals.hpp>   // the SourceHook globals
#include <VoltMod/Unsafe/VtableHook.hpp>

// void CBaseEntity::Teleport(const Vector*, const QAngle*, const Vector*)
VOLTMOD_VHOOK3_VOID(MyPlugin_Teleport, const Vector*, const QAngle*, const Vector*);

class TeleportWatcher
{
    VoltMod::Runtime& _rt;
    VoltMod::VtableHook _hook;

    void Arm()
    {
        // pre only, so post is nullptr; pass a live instance to have its vptr checked
        // against the class table the gamedata name resolved to.
        auto hook = VoltMod::VtableHook::OnVTable<MyPlugin_TeleportHook>(
            "MyPlugin Teleport", _rt.Bindings.PlayerPawn, _rt.Bindings.Teleport.Index(),
            this, &TeleportWatcher::Hook_Teleport, nullptr);
        if (!hook)
        {
            VoltMod::Log::Warn("teleport watch off: {}", hook.error().Detail);
            return;
        }
        _hook = std::move(*hook);   // dropping _hook removes the hook
    }

    void Hook_Teleport(const Vector*, const QAngle*, const Vector*)
    {
        Record(META_IFACEPTR(void));
        RETURN_META(MRES_IGNORED);
    }
};
```

`OnInstance` is the per-object form (SourceHook's `Hook_Normal`): it binds one
live object rather than the class table, which is what @ref VoltMod::Teleport
does per pawn. Re-bind it whenever the object is replaced - a respawn hands the
player a brand-new pawn.

### One hooked vfunc per translation unit

`VOLTMOD_VHOOK*` expands to namespace-scope *definitions* - hook-manager
globals and add/remove functions named after `Name` - so a second translation
unit declaring the same `Name` is a duplicate symbol at link time.

The sharper reason is the descriptor. `SH_MANUALHOOK_RECONFIGURE`, which is how
a gamedata-supplied index reaches the declaration, **mutates the file-static
descriptor `SH_DECL_MANUALHOOK` created**. Two vfuncs sharing one `Name` would
take turns repointing it, and whichever hook was already live would then be
dispatched through the other one's slot. `VtableHook` re-points a declaration
only when the index actually changes (gamedata resolves once per process, so in
practice only the first install does anything), but the rule stands: one `Name`,
one vfunc, one .cpp.

### What it does for you, and what it does not

- **Pair-or-nothing.** Ask for both sides and either being refused leaves
  nothing installed, with the error naming the side that failed.
- **Removal is by id**, so `Reset()` (and the destructor) is safe after a map
  change has already freed every instance the hook was bound to.
- **The class-name cross-check.** Pass a live instance as the last argument and
  its vptr is compared to the resolved class table; a mismatch means the gamedata
  class name drifted, and warns.
- It does **not** validate the index. That is gamedata's job, and re-verifying it
  after a game update is yours - see @ref sdk_gamedata_guide.
- Install from an `Event::Lifecycle` (as the framework services do) rather than
  from `OnLoad` when the hook costs anything per call, so a plugin that never
  subscribes never pays for it.
- Keep the `VtableHook` next to the state its handler touches. A hook that
  outlives its handler's object dispatches into freed memory; one that outlives
  the plugin dispatches into an unloaded module.

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
