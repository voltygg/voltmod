# Movement, teleports, and server commands {#sdk_hooks_guide}

[TOC]

Hook services install for their first subscriber and uninstall after the last.
Check @ref VoltMod::Capabilities "runtime.Capabilities" for gamedata
availability.

## Movement

@ref VoltMod::Movement hooks `CCSPlayer_MovementServices::RunCommand`. `Before` and `After`
bracket one player's movement, and both carry the decoded @ref VoltMod::PlayerInput:

```cpp
// Keep each Subscription beside the state captured by its handler.
_before = runtime.Hooks.Movement.Before += [this](int slot, const VoltMod::PlayerInput& cmd) {
    if (!cmd.Valid)
        return;  // null usercmd or missing gamedata offset
    // cmd.ViewYaw, cmd.MouseDx, cmd.ButtonsHeld, cmd.SubtickMoves[0].YawDelta, ...
};
_after = runtime.Hooks.Movement.After += [this](int slot, const VoltMod::PlayerInput&) { /* restore */ };
```

Hook contracts:

- The DVP hook binds the class vtable and covers current and future players.
- Unresolved gamedata returns an empty `Subscription` and logs the reason.
- Pre and post install atomically, and the command is decoded once per RunCommand.
- The slot is `-1` when its owner cannot be resolved.
- Removal by hook id remains safe after pawn destruction.
- Re-verify the `RunCommand` class and slot after CS2 updates. A wrong slot can crash.
- Re-verify the `UserCmdPB` offset too. A missing offset yields `Valid=false`; a stale one reads
  garbage.

Important fields:

- `CommandNumber` comes from the wrapper because live clients leave protobuf
  `legacy_command_number` at zero. Gaps indicate lost, reordered, or synthesized commands.
- `HasViewAngles=false` means the angle values are defaults, not measurements.
- `ViewRoll` is `viewangles.z`; mouse input drives only pitch and yaw.

### Input history and the cap

`InputHistorySamples` contains per-shot angles and targets. Attack history
indexes refer to entries in the client's full input list.

Only `MaxInputHistory` entries are retained. Use `SampleAt` and never clamp an
out-of-range attack index:

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

Compare against `InputHistoryTotalCount` to distinguish absent, invalid, and capped samples.

### Rewrite: editing the decoded command

`Rewrite` receives a mutable `PlayerInput&` after decoding and before every `Before` handler.
Later handlers observe its edits:

```cpp
_rewrite = runtime.Hooks.Movement.Rewrite += [](int slot, VoltMod::PlayerInput& cmd) {
    cmd.ViewYaw += 90.0f;  // every downstream reader now sees the rotated view
};
```

Edits affect only the decoded snapshot, not the engine's `CUserCmd`. Use this for tests and
diagnostics, not gameplay.

## Teleport

@ref VoltMod::Teleport raises `Teleported(slot)` when a player pawn moves through
`CBaseEntity::Teleport`. Consumers can ignore the resulting discontinuity in motion data.

The service keeps no history; consumers define and store their own grace window:

```cpp
// Subscribing is what installs the hook. PerSlot clears a stamp when the seat changes hands.
_lastTeleport.BindReset(runtime.Slots);
_teleports = runtime.Hooks.Teleport.Teleported += [this](int slot) {
    if (VoltMod::IsValidSlot(slot))
        _lastTeleport[slot] = _rt.Clock.Time();
};

if (!JustTeleported(slot))       // your own window, against your own clock
    EvaluateAim(slot);
```

Semantics worth knowing:

- The first subscription hooks the pawn class vtable, so every pawn is covered at once; missing
  gamedata refuses the subscription after logging why.
- The slot is resolved per call from the pawn's controller, and is `-1` for non-player pawns.
- Respawns need no rebinding: a new pawn shares the class vtable. A spawn also raises the event.
- The hook spans map changes. `runtime.Clock` restarts with the map.

## Hooking a vfunc the framework does not cover

Use `<VoltMod/Unsafe/Hook.hpp>` for vfuncs the framework does not expose.
An incorrect slot can call unrelated code and crash the server.

Custom hooks use two entry points, both yielding the @ref VoltMod::Subscription that removes the
hook when it is dropped:

- `VoltMod::HookInterface` takes a member function pointer and hooks that one interface object.
- `VoltMod::HookVTable` takes a gamedata @ref VoltMod::VHookBinding and hooks every object sharing
  the class vtable. It reports a missing slot or table as an error rather than installing nothing.

A handler is any callable. It takes the hooked object first, as a reference to the class the slot
dispatches on: an engine interface, or one of the `Hooked*` stand-ins in `EngineTypes.hpp` for a
class whose layout the SDK omits. A stand-in is an identity, not a layout, so never dereference
one. A pre-handler returns @ref VoltMod::HookResult, or nothing at all when it only observes; a
post-handler returns nothing and is handed the value the call is about to return.

```cpp
#include <VoltMod/Unsafe/Hook.hpp>

class CommandWatcher
{
    VoltMod::Runtime& _rt;
    VoltMod::Subscription _hook;

    void Install()
    {
        // void* CPlayer_MovementServices::RunCommand(CUserCmd*)
        auto hook = VoltMod::HookVTable("MyPlugin RunCommand", _rt.Unsafe.Bindings.RunCommand,
                                        [this](VoltMod::HookedMovementServices& services, void* userCmd) {
                                            Record(&services, userCmd);
                                        });
        if (!hook)
        {
            VoltMod::Log::Warn("command watch off: {}", hook.error().Detail);
            return;
        }
        _hook = std::move(*hook);
    }
};
```

`VHookBinding` keeps the slot, the class table and the class identity from one gamedata entry
together. Direct calls use its `Method`, a `VFn`, to dispatch through an instance.

### What it does for you, and what it does not

- Pre and post ride one hook, so there is no half-installed pair to unwind.
- `Reset()` remains safe after the hooked object is destroyed; removal never dereferences it.
- An optional live instance detects a mismatched class table.
- The object type is checked at compile time, so a pawn cannot be passed where a client belongs.
- Slot correctness still requires manual verification; see @ref sdk_gamedata_guide.
- Use an `EventLifecycle` for a hook that should exist only while subscribed, or a
  `SharedLifecycle` when several events share the one hook.
- Keep the `Subscription` beside the handler state so their lifetimes match.

## ServerCommand

@ref VoltMod::ServerCommand owns a tier1 `ConCommand`. Construction registers
it, destruction unregisters it, and its handler runs on the game thread.

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

Use server commands for console, RCON, cfg files, and loose automation. For typed plugin contracts,
publish a versioned interface through `runtime.Exchange`. Do not transfer ownership or exceptions
across plugin modules.

Call commands with `runtime.ConVars.ExecuteServerCommand("myplugin_do 765...")`. If the provider is
absent, the engine reports an unknown command.

Construct only while the plugin is loaded (ICvar must be live), typically as a manager member, so unload unregisters it automatically.
