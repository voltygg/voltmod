# Movement, teleports, and server commands {#sdk_hooks_guide}

[TOC]

Hook services install for their first subscriber and uninstall after the last.
Check @ref VoltMod::Capabilities "runtime.Capabilities" for gamedata
availability.

## Movement

@ref VoltMod::Movement hooks `CCSPlayer_MovementServices::RunCommand`. `Pre` and `Post` bracket one
player's movement and suit scoped state changes such as @ref VoltMod::ConVar::RawScope.

```cpp
// Keep each Subscription beside the state captured by its handler.
_pre  = runtime.Hooks.Movement.Pre  += [this](int slot) { /* before movement runs */ };
_post = runtime.Hooks.Movement.Post += [this](int slot) { /* after it ran: restore */ };
```

Hook contracts:

- The DVP hook binds the class vtable and covers current and future players.
- Unresolved gamedata returns an empty `Subscription` and logs the reason.
- Pre and post install atomically.
- The slot is `-1` when its owner cannot be resolved.
- Removal by hook id remains safe after pawn destruction.
- Re-verify the `RunCommand` class and slot after CS2 updates. A wrong slot can crash.

### Cmd events: reading the usercmd

`PreCmd` also provides a @ref VoltMod::UserCmdView. It decodes view angles,
button masks, mouse deltas, and per-subtick angle changes from the
`CSGOUserCmdPB` payload:

```cpp
_preCmd = runtime.Hooks.Movement.PreCmd += [](int slot, const VoltMod::UserCmdView& cmd) {
    if (!cmd.Valid)
        return;  // null usercmd or missing gamedata offset
    // cmd.ViewYaw, cmd.MouseDx, cmd.ButtonsHeld, cmd.SubtickMoves[0].YawDelta, ...
};
```

Decoding runs only for `PreCmd` or `FilterCmd` subscribers. Re-verify the `UserCmdPB` offset after
CS2 updates. A missing offset yields `Valid=false`; a stale offset can read garbage.

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

### FilterCmd: editing the decoded usercmd

`FilterCmd` receives a mutable `UserCmdView&` after decoding and before all
`Pre` and `PreCmd` handlers. Later handlers observe its edits:

```cpp
_filter = runtime.Hooks.Movement.FilterCmd += [](int slot, VoltMod::UserCmdView& cmd) {
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
// Subscribing is what installs the per-pawn hook. PerSlot clears a stamp when the seat changes hands.
_lastTeleport.BindReset(runtime.Slots);
_teleports = runtime.Hooks.Teleport.Teleported += [this](int slot) {
    if (VoltMod::IsValidSlot(slot))
        _lastTeleport[slot] = _rt.Clock.Time();
};

if (!JustTeleported(slot))       // your own window, against your own clock
    EvaluateAim(slot);
```

Semantics worth knowing:

- The first subscription hooks every live pawn; missing gamedata returns an empty subscription.
- The slot is `-1` for non-player pawns.
- Respawns rebind the new pawn and also raise the event.
- Map startup clears bindings. `runtime.Clock` also restarts with the map.

## Hooking a vfunc the framework does not cover

Use `<VoltMod/Unsafe/VtableHook.hpp>` for vfuncs the framework does not expose.
An incorrect slot can call unrelated code and crash the server.

Custom hooks use two pieces:

- `VOLTMOD_VHOOK*` at namespace scope declares the hook and its traits.
- @ref VoltMod::VtableHook owns the installed SourceHook ids and removes them on destruction.

```cpp
#include <VoltMod/Engine/MetamodGlobals.hpp>   // the SourceHook globals
#include <VoltMod/Unsafe/VtableHook.hpp>

// void* CPlayer_MovementServices::RunCommand(CUserCmd*)
VOLTMOD_VHOOK1(MyPlugin_RunCommand, void*, void*);

class CommandWatcher
{
    VoltMod::Runtime& _rt;
    VoltMod::VtableHook _hook;

    void Install()
    {
        auto hook = VoltMod::VtableHook::OnVTable<MyPlugin_RunCommandHook>(
            "MyPlugin RunCommand", _rt.Unsafe.Bindings.RunCommand,
            this, &CommandWatcher::Hook_RunCommand, nullptr);
        if (!hook)
        {
            VoltMod::Log::Warn("command watch off: {}", hook.error().Detail);
            return;
        }
        _hook = std::move(*hook);
    }

    void* Hook_RunCommand(void* userCmd)
    {
        Record(META_IFACEPTR(void), userCmd);
        RETURN_META_VALUE(MRES_IGNORED, nullptr);
    }
};
```

`VHookBinding` keeps the slot and class table from one gamedata entry together. Direct calls use
`VFn` to dispatch through an instance.

`OnInstance` hooks one live object. Rebind when that object is replaced.

### One hooked vfunc per translation unit

`VOLTMOD_VHOOK*` emits namespace-scope definitions and a mutable SourceHook descriptor. Give each
hook a unique name and keep one hooked vfunc per translation unit. Sharing a declaration can
reconfigure a live hook to the wrong slot.

### What it does for you, and what it does not

- A requested pre/post pair installs atomically.
- `Reset()` remains safe after instance destruction because removal uses hook ids.
- An optional live instance detects a mismatched class table.
- Slot correctness still requires manual verification; see @ref sdk_gamedata_guide.
- Use `Event::Lifecycle` for hooks that should exist only while subscribed.
- Keep `VtableHook` beside the handler state so their lifetimes match.

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
