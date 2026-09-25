# Entities and players {#sdk_players_guide}

[TOC]

```cpp
auto pawn = runtime.Entities.Pawn(slot);
if (!pawn)
    return;

pawn.SetHealth(100);                              // writes m_iHealth and dirties it for replication
pawn.SetFlags(pawn.Flags() | FL_ONGROUND);

auto controller = runtime.Entities.Controller(slot);
controller.ChangeTeam(VoltMod::Team::CT);
controller.Kick("Cheating");
```

## The object model

Three value types wrap a live entity, each adding to the one before it:

| Type | What it is | Where it comes from |
| ---- | ---------- | ------------------- |
| @ref VoltMod::Entity | Any entity: the CBaseEntity fields, position, teleport | `runtime.Entities.Get(ref)`, `Find`, `FindAll`, `entity.AsPawn()` |
| @ref VoltMod::Pawn | A player's body: health, armor, movement, aim, render | `runtime.Entities.Pawn(slot)`, `Pawn(ref)` |
| @ref VoltMod::Controller | A player's identity: name, money, team, kick | `runtime.Entities.Controller(slot)` |

The controller is the scoreboard identity and survives respawns; the pawn is the replaceable body.
CBaseEntity fields read off a `Controller` belong to the controller entity and mean nothing for
gameplay - use `controller.Pawn()`.

## Validity, and never storing a wrapper

Every wrapper is a frame-local value around a raw entity pointer, and the engine frees entities
between frames without telling anyone.

- `explicit operator bool()` is the one validity check. There is no `IsValid()`.
- Never store a wrapper. Store an @ref VoltMod::EntityRef (index + serial) or a
  @ref VoltMod::PlayerRef (slot + SteamID) and resolve it again where you need it.
- Wrappers copy but do not assign, so a rebind can never read like a field write.

```cpp
// Wrong: the pawn is gone by the time the timer fires.
auto pawn = runtime.Entities.Pawn(slot);
scheduler.Delay(3000, [pawn] { pawn.SetHealth(100); });

// Right: re-resolve on the far side.
scheduler.Delay(3000, [&entities = runtime.Entities, slot] {
    if (auto pawn = entities.Pawn(slot))
        pawn.SetHealth(100);
});
```

A falsy wrapper reads zero and ignores writes. A stale non-null pointer does neither, which is why
the rule is "never store one" rather than "check before use".

## Fields

Schema fields are generated accessor pairs, not data members: `pawn.Health()` reads,
`pawn.SetHealth(100)` writes and replicates. `voltmod framework schemagen` bakes the offsets in from a schema
dump, and the load aborts when the live game no longer matches - see
@ref sdk_gamedata_guide "Gamedata and schema".

```cpp
if (pawn.Team() == VoltMod::Team::CT && (pawn.Flags() & FL_ONGROUND))
    pawn.SetSpeedModifier(1.5f);

int money = controller.InGameMoneyServices().Account();   // a sub-object is a hop, not a follow
```

Fields on a pawn's services are reached the same way. The camera services pointer is typed as the
base class, so view it as the CS subclass to reach the zoom fields:

```cpp
pawn.SetGravityScale(0.5f);                                // CBaseEntity, so any entity has it
pawn.MovementServices().SetMaxSpeed(300.0f);
pawn.CameraServices().SetViewEntityRef(camera.Ref());     // see through another entity
VoltMod::EntityRef zoomOwner = VoltMod::Schema::CCSPlayerBase_CameraServices{pawn.CameraServices().Base()}.ZoomOwnerRef();
```

A handle field reads as an `EntityRef`, and its name ends in `Ref`. Get the entity it points at:

```cpp
// m_hOwnerEntity: for a thrown grenade, the pawn that threw it
VoltMod::Entity owner = runtime.Entities.Get(grenade.OwnerRef());
```

An entity with no wrapper class is viewed through its generated class. A `beam` draws a line
from its origin to its end point; set the fields before it spawns:

```cpp
VoltMod::Entity line = runtime.Entities.Create("beam");
const VoltMod::Schema::CBeam beam{line.Raw()};
beam.SetWidth(2.0f);
beam.SetEndWidth(2.0f);
beam.SetEndPos(end);
VoltMod::KeyValues kv;
kv.Set("origin", start);
line.Spawn(kv);
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

VoltMod::Controller controller = es.Controller(slot);   // falsy when the slot is empty
VoltMod::Pawn pawn = es.Pawn(slot);                     // falsy while dead

uint64_t buttons = controller.Buttons();   // held buttons, the SDK's IN_* bits
int owner = pawn.Slot();                   // -1 when it is not a player pawn; constant time
VoltMod::Pawn victim = hit.Victim.AsPawn(); // falsy unless the entity is a player pawn

VoltMod::EntityRef ref = pawn.Ref();       // storable
VoltMod::Entity again = es.Get(ref);       // falsy if it died or its index was recycled
VoltMod::Pawn body = es.Pawn(ref);         // the same, and falsy unless it is a player pawn

VoltMod::Entity rules = es.Find("cs_gamerules");          // the first of a class
for (const VoltMod::Entity& door : es.FindAll("func_door")) // a snapshot: removing is safe
    door.Remove();
for (const VoltMod::Pawn& alive : es.AlivePawns())          // every living player, in slot order
    alive.Heal(10);
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
`SpottedState()` exposes the radar bits, and `WeaponServices().ActiveWeaponRef()` is the held
weapon.

`Vector` and `QAngle` are the engine's own types; `<VoltMod/Engine/Math.hpp>` is the header to
include for them, so a plugin never names an SDK path. `AngleToForward`, from the same header,
turns an aim into the unit vector it points along, for tracing or placing something ahead of a
player.

```cpp
QAngle aim = pawn.EyeAngles();
Vector muzzle = pawn.EyePosition();
Vector ahead = muzzle + VoltMod::AngleToForward(aim) * 64.0f;

using VoltMod::ObserverMode;
if (pawn.ObserverMode() != ObserverMode::Roaming)
    pawn.SetObserverMode(ObserverMode::Roaming);
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
const VoltMod::Pawn self = runtime.Entities.Pawn(slot);
const VoltMod::Pawn other = runtime.Entities.Pawn(target);
const auto clear = runtime.Trace.Clear(self.EyePosition(), other.EyePosition(),
                                             {.Ignore1 = self, .Ignore2 = other});
if (clear && *clear)
    ...  // nothing solid between the two eyes
```

`Line` returns a @ref VoltMod::TraceHit saying where the trace stopped, the surface normal there,
`HitEntity`, the entity it hit, and `HitWorld` when that was the map itself; `Clear` is the yes/no
form. `FromEyes(pawn, distance)` traces along a player's aim, ignoring the player.
`TraceOptions::Layers` picks what stops it: `Sight` (world geometry and line-of-sight blockers, so
windows and clips do not count) or `Solid` (what a player body collides with, other players
included). `IgnoreOwnedBy` passes through everything an entity owns: spawn a multi-part prop with
`PropSpec::Owner` set to its first part, and one trace ignores the whole prop.

`Box` sweeps a box instead of a line. A sweep that starts and ends at one point asks whether a
box of that size fits there:

```cpp
const Vector mins(-16.0f, -16.0f, 1.0f), maxs(16.0f, 16.0f, 48.0f);   // just clear of the floor
const auto blocked = runtime.Trace.Box(spot, spot, mins, maxs, {.Layers = VoltMod::TraceLayers::Solid});
if (blocked && !blocked->Hit)
    ...  // nothing solid, players included, overlaps the box at spot
```

`Box` needs its own vtable slot, so it can be unsupported while `Line` works.

## Ending a round

@ref VoltMod::Rounds ends the current round through `CCSGameRules::TerminateRound`, so the win
panel, `round_end` and the next round are the engine's own. It works with
`mp_ignore_round_win_conditions 1`, which is how a mode with its own win rule runs:

```cpp
// Terrorists win; the next round starts in 5 seconds.
runtime.Rounds.End(VoltMod::RoundEndReason::TerroristsWin, 5.0f);
```

Team scores are left alone.

## Teams

`VoltMod::Team` (`<VoltMod/Engine/Team.hpp>`, no SDK) is what `Team()` returns and `ChangeTeam`
takes. The team lives on the controller:

```cpp
VoltMod::Controller controller = runtime.Entities.Controller(slot);
if (VoltMod::IsPlaying(controller.Team()))
    controller.ChangeTeam(VoltMod::Opposite(controller.Team()));
controller.ChangeTeam(VoltMod::Team::Spectator);
```

## Pawn verbs

```cpp
pawn.GiveItem("weapon_ak47");                 // an entity class name, not a display name
pawn.StripWeapons();                          // pass false to keep armor and the defuse kit
for (const VoltMod::Entity& weapon : pawn.Weapons())
    weapon.SetRender(RenderMode_t::kRenderTransAlpha, VoltMod::Color{.A = 0});

pawn.SetGodmode(!pawn.Godmode());             // FL_GODMODE: the pawn takes no damage
pawn.SetMoveType(MoveType_t::MOVETYPE_NOCLIP); // writes both move-type fields
pawn.Launch(Vector{0.0f, 0.0f, 600.0f});      // velocity, and off the ground this tick
pawn.Heal(25);                                // up to MaxHealth
```

The engine refuses a weapon the pawn's team cannot buy, so `GiveItem` retries with the pawn on the
other team for the same frame and swaps it back before returning. It returns false when the engine
refused the item both times or the item services did not bind.

## Spawning, entity IO and sound

`runtime.Entities` creates entities; the verbs are on the @ref VoltMod::Entity itself. Build spawn
data with @ref VoltMod::KeyValues. `runtime.Entities.Available()` says whether spawning bound.

```cpp
VoltMod::KeyValues kv;
kv.Set("origin", pos).Set("spawnflags", 1);
if (VoltMod::Entity boom = runtime.Entities.Spawn("env_explosion", kv))
{
    boom.AcceptInput("Explode");      // fire an input now
    boom.RemoveAfter(1.0f);           // a "Kill" through the engine's input queue
}

entity.EmitSound("SoundEventName");   // a .vsndevts event name, not a file path
prop.SetModel("models/props/crate.vmdl");
prop.SetScale(1.5f);
prop.PlayAnimation("open", "idle");
```

Never `delete` an entity: use `Remove` or `RemoveAfter`. Fields written between `Create` and
`Spawn` go out with the first snapshot.

Props, particles and beams have their own spawners, which know the engine's traps: a prop that
blocks nothing needs collision turned off as well as `solid 0`, and a line is a `beam`, never an
`env_beam`, which takes the server down.

```cpp
VoltMod::Entity crate = runtime.Entities.SpawnProp({.Model = "models/props/crate.vmdl", .Origin = pos,
                                                    .Solid = false, .Scale = 0.5f});
runtime.Entities.SpawnParticle("particles/explosion.vpcf", pos).RemoveAfter(2.0f);
runtime.Entities.SpawnBeam(from, to, 2.0f, VoltMod::Color{255, 60, 30});
```

## Precache

@ref VoltMod::Precache queues custom resources for the next map's session manifest. The host hooks
the game rules system's manifest event once and hands every plugin the manifest, so a plugin loaded
mid-map, by `volt reload` too, adds its resources at the next map load.

```cpp
runtime.Precache.Add("particles/my_plugin/lightning_strike.vpcf");
```

Assets that are not part of the map must also reach clients, such as through a workshop addon, or
they precache server-side and render nothing.
