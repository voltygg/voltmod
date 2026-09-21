# Entities and players {#sdk_players_guide}

[TOC]

```cpp
auto pawn = runtime.Entities.PawnOf(slot);
if (!pawn)
    return;

pawn.SetHealth(100);                              // writes m_iHealth and dirties it for replication
pawn.SetFlags(pawn.Flags() | VoltMod::FL_ONGROUND);

auto controller = runtime.Entities.Controller(slot);
controller.ChangeTeam(VoltMod::TeamCT);
controller.Kick("Cheating");
```

## The object model

Three value types wrap a live entity, each adding to the one before it:

| Type | What it is | Where it comes from |
| ---- | ---------- | ------------------- |
| @ref VoltMod::Entity | Any entity: the CBaseEntity fields, position, teleport | `runtime.Entities.Resolve(ref)`, `FindByClassName`, `FindByName` |
| @ref VoltMod::Pawn | A player's body: health, armor, movement, aim, render | `runtime.Entities.PawnOf(slot)` |
| @ref VoltMod::Controller | A player's identity: name, money, team, kick | `runtime.Entities.Controller(slot)` |

The controller is the scoreboard identity and survives respawns; the pawn is the replaceable body.
CBaseEntity fields read off a `Controller` belong to the controller entity and mean nothing for
gameplay - use `controller.GetPawn()`.

## Validity, and never storing a wrapper

Every wrapper is a frame-local value around a raw entity pointer, and the engine frees entities
between frames without telling anyone.

- `explicit operator bool()` is the one validity check. There is no `IsValid()`.
- Never store a wrapper. Store an @ref VoltMod::EntityRef (index + serial) or a
  @ref VoltMod::PlayerRef (slot + SteamID) and resolve it again where you need it.
- Wrappers copy but do not assign, so a rebind can never read like a field write.

```cpp
// Wrong: the pawn is gone by the time the timer fires.
auto pawn = runtime.Entities.PawnOf(slot);
scheduler.Delay(3000, [pawn] { pawn.SetHealth(100); });

// Right: re-resolve on the far side.
scheduler.Delay(3000, [&entities = runtime.Entities, slot] {
    if (auto pawn = entities.PawnOf(slot))
        pawn.SetHealth(100);
});
```

A falsy wrapper reads zero and ignores writes. A stale non-null pointer does neither, which is why
the rule is "never store one" rather than "check before use".

## Fields

Schema fields are generated accessor pairs, not data members: `pawn.Health()` reads,
`pawn.SetHealth(100)` writes and replicates. `voltmod schemagen` bakes the offsets in from a schema
dump, and the load aborts when the live game no longer matches - see
@ref sdk_gamedata_guide "Gamedata and schema".

```cpp
if (pawn.Team() == VoltMod::TeamCT && (pawn.Flags() & VoltMod::FL_ONGROUND))
    pawn.SetSpeedModifier(1.5f);

int money = controller.InGameMoneyServices().Account();   // a sub-object is a hop, not a follow
```

Fields on a pawn's services are reached the same way. The camera services pointer is typed as the
base class, so view it as the CS subclass to reach the zoom fields:

```cpp
pawn.SetGravityScale(0.5f);                                // CBaseEntity, so any entity has it
pawn.MovementServices().SetMaxSpeed(300.0f);
pawn.CameraServices().SetViewEntity(camera.Ref().Handle);  // see through another entity
VoltMod::Schema::CCSPlayerBase_CameraServices{pawn.CameraServices().Base()}.SetFieldOfView(90);
```

An entity with no wrapper class is viewed through its generated class. A `beam` draws a line
from its origin to its end point; set the fields before it spawns:

```cpp
CEntityInstance* line = runtime.World.EntityOps.CreateByName("beam");
const VoltMod::Schema::CBeam beam{line};
beam.SetWidth(2.0f);
beam.SetEndWidth(2.0f);
beam.SetEndPos(end);
VoltMod::KeyValues kv;
kv.Set("origin", start);
runtime.World.EntityOps.DispatchSpawn(line, &kv);
```

A `Set` on a networked field dirties it for the next snapshot. A field the engine does not network
is written without a notify, because the engine rejects one and then stops updating that entity for
its clients; a field with no route to notify generates no setter at all.

Adding a field means editing `schema/manifest.json` and regenerating. An entry is `m_name`,
`m_name>Accessor` to rename it, `m_name:CppType` to read it as that type, or
`m_name>Accessor:CppType`; a class set to `"*"` takes every field the dump reports. For a class
with no curated wrapper, construct its generated view over the raw pointer:

```cpp
VoltMod::Schema::CCSPlayerPawn view{pawn.Raw()};
view.SetArmor(100);
```

Every accessor answers harmlessly on a falsy view. Use them only on the game thread.

## EntitySystem

`runtime.Entities` is both the factory and the lookup:

```cpp
auto& es = runtime.Entities;

VoltMod::Controller controller = es.Controller(slot);
VoltMod::Pawn pawn = es.PawnOf(slot);
bool occupied = es.IsPlayerSlotValid(slot);

uint64_t buttons = es.Buttons(slot);       // held buttons, IN_* flags
int owner = es.SlotOf(pawn);               // -1 when it is not a player pawn; constant-time

VoltMod::EntityRef ref = pawn.Ref();       // storable
VoltMod::Entity again = es.Resolve(ref);   // falsy if it died or its index was recycled

// Iterate map entities; a falsy Entity starts at the list head, and the walk ends falsy.
for (auto door = es.FindByClassName({}, "func_door"); door; door = es.FindByClassName(door, "func_door"))
    /* ... */;
auto named = es.FindByName({}, "my_targetname");
```

## Aim, flash and observer state

`EyeAngles()` is the pawn's networked aim and `EyePosition()` is the origin plus `ViewOffset()`,
where shots originate. `FlashDuration()` and `FlashMaxAlpha()` carry what the last `player_blind`
set, 255 max-alpha meaning a full blind; for blind-time bookkeeping prefer the typed `PlayerBlind`
event, which carries the duration directly.

`ShotsFired()` counts the current burst and the engine resets it once the player stops firing.
`LastWeaponFireCommand()` is the usercmd number of the last shot, which ties a `weapon_fire` event
to its command. `AimPunchServices()` carries the recoil punch as the last shot set it, so read
`BaseAngle()` and `BaseTick()` together - the engine decays the angle from that base each tick.
`SpottedState()` exposes the radar bits, and `WeaponServices().ActiveWeapon()` is a handle to
resolve through `EntitySystem::Resolve`.

`AngleToForward` (`<VoltMod/Entities/Angles.hpp>`) turns an aim into the unit vector it points
along, for tracing or placing something ahead of a player.

```cpp
QAngle aim = pawn.EyeAngles();
Vector muzzle = pawn.EyePosition();
Vector ahead = muzzle + VoltMod::AngleToForward(aim) * 64.0f;

using VoltMod::ObserverMode_t;
if (pawn.GetObserverMode() != ObserverMode_t::Roaming)
    pawn.SetObserverMode(ObserverMode_t::Roaming);
```

Observer mode is a method rather than a field: it lives on a sub-object the pawn points at, so
there is no fixed offset to reach it.

## The scoreboard name

`Name()` is `m_iszPlayerName`, a 128-byte fixed buffer. `SetName` truncates to 127 characters plus
NUL, and replication piggybacks on the next state-change broadcast, so pair a write with
`ChangeTeam` or similar when the scoreboard has to refresh now.

```cpp
std::string saved{controller.Name()};   // the view borrows the buffer; copy what you keep
controller.SetName("");                 // hide on the scoreboard
controller.SetName(saved);
```

## Traces

@ref VoltMod::Trace answers sight and reachability questions through the nav mesh's window onto the
physics world. Nothing to install, nothing to re-take per map, and it survives map changes.

```cpp
const VoltMod::Pawn self = runtime.Entities.PawnOf(slot);
const VoltMod::Pawn other = runtime.Entities.PawnOf(target);
const auto clear = runtime.World.Trace.Clear(self.EyePosition(), other.EyePosition(),
                                             {.Ignore1 = self.Raw(), .Ignore2 = other.Raw()});
if (clear && *clear)
    ...  // nothing solid between the two eyes
```

`Line` returns a @ref VoltMod::TraceHit saying where the trace stopped, the surface normal there,
and `HitEntity`, the entity it hit (the world included); `Clear` is the yes/no form.
`TraceOptions::Layers` picks what stops it: `Sight` (world geometry and line-of-sight blockers, so
windows and clips do not count) or `Solid` (what a player body collides with, other players
included).

`Hull` sweeps a box instead of a line. A sweep that starts and ends at one point asks whether a
box of that size fits there:

```cpp
const Vector mins(-16.0f, -16.0f, 1.0f), maxs(16.0f, 16.0f, 48.0f);   // just clear of the floor
const auto blocked = runtime.World.Trace.Hull(spot, spot, mins, maxs, {.Layers = VoltMod::TraceLayers::Solid});
if (blocked && !blocked->Hit)
    ...  // nothing solid, players included, overlaps the box at spot
```

`Hull` needs its own vtable slot, so it can be unsupported while `Line` works.

## Ending a round

@ref VoltMod::Rounds ends the current round through `CCSGameRules::TerminateRound`, so the win
panel, `round_end` and the next round are the engine's own. It works with
`mp_ignore_round_win_conditions 1`, which is how a mode with its own win rule runs:

```cpp
// Terrorists win; the next round starts in 5 seconds.
runtime.World.Rounds.End(VoltMod::RoundEndReason::TerroristsWin, 5.0f);
```

Team scores are left alone.

## PawnOps

Common pawn manipulations, as free functions in `VoltMod::PawnOps`
(`<VoltMod/Entities/PawnOps.hpp>`), plus the engine team constants `TeamNone` / `TeamSpectator` /
`TeamT` / `TeamCT`:

```cpp
namespace PawnOps = VoltMod::PawnOps;

VoltMod::Pawn target = runtime.Entities.PawnOf(slot);

PawnOps::ToggleNoclip(target);              // noclip <-> walk; returns the new on-state
PawnOps::ToggleFreeze(target);              // MoveType None <-> walk
PawnOps::ToggleGodmode(target);             // FL_GODMODE flip, the working CS2 invincibility path
PawnOps::ShiftZ(target, -15.0f);            // bury; +15 to unbury

// Team lives on the controller, so this one takes that.
PawnOps::ChangeTeamSafe(runtime.Entities.Controller(slot), VoltMod::TeamCT);

// Teleports: a destination cleared past the anchor's hull, and an exact-origin swap.
Vector dest = PawnOps::ClearedDestination(anchor);   // 48u ahead of the anchor's eye yaw
target.Teleport(dest, std::nullopt, Vector{0, 0, 0});
PawnOps::SwapOrigins(a, b);                          // both spots vacate in the same frame
```

Anything needing framework services lives on `runtime.World.Pawns` instead:

```cpp
runtime.World.Pawns.Slap(target);           // upward punt plus timed fall protection
runtime.World.Pawns.SlayDelayed(slot, 2000);
```

Both drop their pending work when the seat changes hands, so neither can reach the next occupant.

## Items

@ref VoltMod::Items gives and strips weapons through the pawn's `CCSPlayer_ItemServices`. Both are
vtable calls whose indices live in gamedata, so a game update is a gamedata edit rather than a
rebuild.

```cpp
runtime.World.Items.Give(target, "weapon_ak47");   // entity classname, not a display name
runtime.World.Items.StripWeapons(target);          // pass false to keep armor and the defuse kit
```

`Give` returns false only when the pawn is unavailable or the engine refused the item twice. The
retry matters: the engine rejects a weapon the player's team cannot buy, so a refusal is tried
again with the pawn briefly flipped to the other team and flipped back before the call returns.
Do not interleave it with anything else that reads the pawn's team.

Anything that fails to resolve - the pawn, the item services pointer, the vtable index - degrades
the call to `false`.

## Spawning, entity IO and sound

@ref VoltMod::EntityOps creates, mutates and removes entities. Build spawn data with
@ref VoltMod::KeyValues; `CanSpawn()` checks the bindings `Spawn()` needs.

```cpp
auto& ops = runtime.World.EntityOps;

VoltMod::KeyValues kv;
kv.Set("origin", pos).Set("spawnflags", 1);
if (auto* boom = ops.Spawn("env_explosion", kv))
{
    ops.AcceptInput(boom, "Explode");      // fire an input now
    ops.RemoveDelayed(boom, 1.0f);         // deferred "Kill" through the engine's IO queue
}

ops.EmitSound(entity, "SoundEventName");   // a .vsndevts event name, not a file path
```

Never `delete` an entity: use `Remove` or `RemoveDelayed`. Fields written before `DispatchSpawn` go
out with the first snapshot, so a generated view's setter costs nothing extra there:

```cpp
VoltMod::Schema::CBeam{beam}.SetWidth(2.0f);
```

## Precache

@ref VoltMod::Precache queues custom resources for the next map's session manifest. `Runtime::Initialize`
registers the game system and unload detaches it.

```cpp
runtime.World.Precache.Add("particles/my_plugin/lightning_strike.vpcf");
```

Assets that are not part of the map must also reach clients, such as through a workshop addon, or
they precache server-side and render nothing.
