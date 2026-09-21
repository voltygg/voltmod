# Movement, teleports, damage, and server commands {#sdk_hooks_guide}

[TOC]

Hook services install for their first subscriber and uninstall after the last. Each one's
`Available()` says whether its gamedata bound, and why not.

## Movement

@ref VoltMod::Movement hooks `CCSPlayer_MovementServices::RunCommand`. `Before` and `After` bracket
one player's movement and both carry the decoded @ref VoltMod::PlayerInput:

```cpp
// Keep each Subscription beside the state its handler captures.
_before = runtime.Hooks.Movement.Before += [this](int slot, const VoltMod::PlayerInput& cmd) {
    if (!cmd.Valid)
        return;  // null usercmd, or the CSGOUserCmdPB offset did not bind
    // cmd.ViewYaw, cmd.MouseDx, cmd.ButtonsHeld, cmd.SubtickMoves[0].YawDelta, ...
};
_after = runtime.Hooks.Movement.After += [this](int slot, const VoltMod::PlayerInput&) { /* restore */ };
```

The hook binds the class vtable, so it covers current and future players at once, and the command
is decoded once per `RunCommand`. The slot is `-1` when its owner cannot be resolved. Unresolved
gamedata returns an empty `Subscription` and logs the reason; a wrong `RunCommand` slot can crash,
so re-verify it after a CS2 update.

Fields worth knowing:

- `CommandNumber` comes from the `CUserCmdBase::cmdNum` offset, because live clients leave the
  protobuf `legacy_command_number` at zero. Gaps mean lost, reordered or synthesized commands.
- `HasViewAngles == false` means the angle fields hold defaults, not measurements.
- `ViewRoll` is `viewangles.z`; mouse input drives only pitch and yaw.

### Input history and the cap

`InputHistorySamples` holds per-shot angles and claimed targets. Attack indices address the
client's full input list, but only `MaxInputHistory` (16) entries are kept, so use `SampleAt` and
never clamp an out-of-range index:

```cpp
const int index = cmd.Attack1StartHistoryIndex;
if (const auto shot = cmd.SampleAt(index))
    Compare(shot->ViewYaw, cmd.ViewYaw);            // the entry is present
else if (index >= cmd.InputHistoryTotalCount)
    /* the client named an entry it never sent: a malformed command */;
else if (index >= 0)
    /* a shot happened but its angles were capped away, so no verdict */;
// otherwise the index is -1: no attack started this command
```

`InputHistoryTotalCount` is what the client sent before the cap; it is what tells absent, invalid
and capped samples apart.

### Rewrite

`Rewrite` receives a mutable `PlayerInput&` after decoding and before every `Before` handler, so
later handlers see its edits:

```cpp
_rewrite = runtime.Hooks.Movement.Rewrite += [](int slot, VoltMod::PlayerInput& cmd) {
    cmd.ViewYaw += 90.0f;  // every downstream reader now sees the rotated view
};
```

Edits change only the decoded snapshot, not the engine's `CUserCmd`. Use it for tests and
diagnostics, not gameplay.

## Teleport

@ref VoltMod::Teleport raises `Teleported(slot)` when a player pawn moves through
`CBaseEntity::Teleport`, so consumers can ignore the resulting discontinuity in motion data. The
service keeps no history; store your own window.

```cpp
// Subscribing is what installs the hook. PerSlot clears a stamp when the seat changes hands.
_lastTeleport.BindReset(runtime.Slots);
_teleports = runtime.Hooks.Teleport.Teleported += [this](int slot) {
    if (VoltMod::IsValidSlot(slot))
        _lastTeleport[slot] = _rt.Clock.Time();
};
```

The first subscription hooks the pawn class vtable, so every pawn is covered and respawns need no
rebinding. Spawning also raises the event, so filter it out if you only want mid-life teleports.
The slot is `-1` for non-player pawns. The hook spans map changes, but `runtime.Clock` restarts
with the map.

## Damage

@ref VoltMod::Damage hooks `CBaseEntity::TakeDamageOld`, which every entity's damage passes
through: players, props, and hits dealt by `Apply`. `Before` receives a @ref VoltMod::DamageHit
before the engine applies it:

```cpp
_damage = runtime.Hooks.Damage.Before += [this](VoltMod::DamageHit& hit) {
    if (!IsStructure(hit.Victim.Ref()))
        return;
    hit.Blocked = true;                        // the engine deals nothing and fires no event
    Wear(hit.Victim.Ref(), hit.Info.Attacker, hit.Info.Amount);
};
```

Edits to `hit.Info.Amount` and `hit.Info.Type` reach the engine; `Attacker` and `Inflictor` are
for reading.

`Apply` deals damage through the same engine path, so death, the kill feed and `player_death`
credit the attacker as if their own weapon had hit:

```cpp
runtime.Hooks.Damage.Apply(target, {.Attacker = owner.Ref(),     // credited in the kill feed
                                    .Inflictor = turret,          // empty means the attacker
                                    .Amount = 25.0f,
                                    .Type = VoltMod::DamageBullet});
```

The engine drops a hit with no inflictor, so an empty `Inflictor` falls back to the attacker.
Both need the `CBaseEntity::TakeDamageOld` and `CTakeDamageInfo::CTakeDamageInfo` signatures;
`Available()` says which one did not bind.

## Hooking a vfunc the framework does not cover

`<VoltMod/Unsafe/Hook.hpp>` has three entry points. Each yields the @ref VoltMod::Subscription that
removes the hook when dropped; the two gamedata-bound ones wrap it in a `Result`, because an
unbound slot or signature is an error rather than a silent no-op.

| Entry point | Hooks | Use for |
| --- | --- | --- |
| `HookInterface(&Iface::Method, instance, before[, after])` | that one instance | a named SDK interface you hold a pointer to |
| `HookVirtual(name, bindings.Member, before[, after])` | calls through the bound class vtable | a virtual function located by gamedata |
| `HookFunction(name, bindings.Member, before[, after])` | the function where its code starts | a non-virtual function |

Prefer `HookVirtual` for anything virtual. A hook placed where the code starts catches every
caller, and the compiler often folds many classes onto one body - a slot returning `false` can be
the same code in hundreds of unrelated classes.

A handler is any callable taking the hooked object first, as a reference to the class the slot
dispatches on: an engine interface, or one of the `Engine*` stand-ins in `EngineTypes.hpp` for a
class whose layout the SDK omits. A stand-in is an identity, not a layout, so never dereference
one. A before-handler returns @ref VoltMod::HookResult, or nothing when it only observes; an
after-handler takes the same arguments and returns nothing.

```cpp
#include <VoltMod/Unsafe/Hook.hpp>

// void* CPlayer_MovementServices::RunCommand(CUserCmd*)
auto hook = VoltMod::HookVirtual("MyPlugin RunCommand", _rt.Unsafe.Bindings.RunCommand,
                                 [this](VoltMod::EngineMovementServices& services, void* userCmd) {
                                     Record(&services, userCmd);
                                 });
if (!hook)
{
    VoltMod::Log::Warn("command watch off: {}", hook.error().Detail);
    return;
}
_hook = std::move(*hook);
```

A gamedata `base` counts the slot in a base class's own table, so a function on a secondary base is
hooked there and the handler receives that base. An unbound slot is reported as an error rather
than installing nothing.

What the hook layer does and does not do:

- Both handlers ride one hook, so there is no half-installed pair to unwind.
- `Reset()` stays safe after the hooked object is destroyed; removal never dereferences it.
- The object type is checked at compile time, so a pawn cannot be passed where a client belongs.
- Slot correctness is still yours to verify; see @ref sdk_gamedata_guide.
- Use an `EventLifecycle` for a hook that should exist only while subscribed, or a
  `SharedLifecycle` when several events share one hook.
- Keep the `Subscription` beside the handler state so their lifetimes match.

## ServerCommand

@ref VoltMod::ServerCommand owns a tier1 `ConCommand`. Construction registers it, destruction
unregisters it, and the handler runs on the game thread. Construct it only while the plugin is
loaded (ICvar must be live), typically as a manager member so unload cleans it up.

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

Call one with `runtime.ConVars.ExecuteServerCommand("myplugin_do 765...")`; the engine reports an
unknown command when no provider is loaded. Server commands are for console, RCON, cfg files and
loose automation. For a typed contract between two plugins publish a versioned interface through
`runtime.Exchange` instead, and never transfer ownership or exceptions across module boundaries.
