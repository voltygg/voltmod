# Entities and players {#sdk_players_guide}

[TOC]

## The object model

Three value types wrap a live entity, each adding to the one before it:

| Type | What it is | Where it comes from |
| ---- | ---------- | ------------------- |
| @ref VoltMod::Entity | Any entity: the CBaseEntity fields, position, teleport | `runtime.Entities.Resolve(ref)`, `FindByClassName`, `FindByName` |
| @ref VoltMod::Pawn | A player's body: health, armor, movement, aim, render | `runtime.Entities.PawnOf(slot)` |
| @ref VoltMod::Controller | A player's identity: name, money, team, kick | `runtime.Entities.Controller(slot)` |

The controller represents the scoreboard identity and survives respawns. The
pawn is the replaceable body and holds health, armor, movement, and aim. Base
entity fields inherited by `Controller` belong to the controller entity, not the
player's body.

```cpp
auto pawn = runtime.Entities.PawnOf(slot);
if (!pawn)
    return;

int hp = pawn.Health;          // reads m_iHealth
pawn.Health = 100;             // writes it *and* dirties it for replication
pawn.Flags |= VoltMod::FL_ONGROUND;   // |= and &= work on integral fields

auto controller = runtime.Entities.Controller(slot);
controller.Kick("Cheating");
controller.ChangeTeam(VoltMod::TeamCT);
```

## Validity, and never storing a wrapper

Every wrapper is a **frame-local value** around a raw entity pointer. The engine
may free that entity between frames.

- `explicit operator bool()` is the one validity check. There is no `IsValid()`.
- **Never store a wrapper.** Store an @ref VoltMod::EntityRef (index + serial, validated on
  resolve) or a @ref VoltMod::PlayerRef (slot + SteamID), and resolve it again where you need it.
- Wrappers copy but do not assign, so a rebind can never be mistaken for a field write.

```cpp
// Wrong: the pawn is gone by the time the timer fires.
auto pawn = runtime.Entities.PawnOf(slot);
scheduler.Delay(3000, [pawn] { pawn.Health = 100; });

// Right: re-resolve on the far side.
scheduler.Delay(3000, [&entities = runtime.Entities, slot] {
    if (auto pawn = entities.PawnOf(slot))
        pawn.Health = 100;
});
```

A falsy wrapper reads zero values and ignores writes. A stale non-null pointer is
not safe, which is why wrappers must never be stored.

## Fields

A member declared as @ref VoltMod::Field is a schema field used as if it were a data member. It
converts to its type on read, takes its type on write, and compares directly:

```cpp
if (pawn.Team == VoltMod::TeamCT && pawn.Flags.Get() & VoltMod::FL_ONGROUND)
    pawn.SpeedModifier = 1.5f;
```

Compared with a raw offset, `Field`:

1. **Resolves once per process.** Each `(class, field)` lookup is cached for the
   server process.
2. **Walks base classes.** A field declared on a base resolves from its derived
   entity class.
3. **Replicates networked writes.** The field is dirtied for the next snapshot.

`Field::Ref()` exposes the resolved @ref VoltMod::FieldRef (offset, size, networked, chain), and
`Get()` / `Set()` are there when the conversion operators read badly at a call site.

For a field with no wrapper to hang it on, declare a typed `static` @ref VoltMod::SchemaField and reach
it through a @ref VoltMod::SchemaPtr - see @ref sdk_gamedata_guide "Gamedata".

## EntitySystem

`runtime.Entities` is the factory and the lookup:

```cpp
auto& es = runtime.Entities;

VoltMod::Controller controller = es.Controller(slot);
VoltMod::Pawn pawn = es.PawnOf(slot);
bool occupied = es.IsPlayerSlotValid(slot);

uint64_t buttons = es.Buttons(slot);       // held buttons, IN_* flags
int owner = es.SlotOf(pawn);               // -1 when it is not a player pawn; constant-time

// Storable references, resolved back through the entity system.
VoltMod::EntityRef ref = pawn.Ref();
VoltMod::Entity again = es.Resolve(ref);   // falsy if the entity died or its index was recycled

// Iterate map entities; a falsy Entity starts at the list head, and the walk ends falsy.
for (auto door = es.FindByClassName({}, "func_door"); door; door = es.FindByClassName(door, "func_door"))
    /* ... */;
auto named = es.FindByName({}, "my_targetname");
```

## Aim, flash and observer state

`EyeAngles` is the pawn's networked aim, `EyePosition()` is the origin plus `ViewOffset` (where
shots originate), and `FlashDuration` / `FlashMaxAlpha` carry what the last `player_blind` set
(255 max-alpha means a full blind). For blind-time bookkeeping prefer the typed `PlayerBlind`
event, which carries the duration directly.

```cpp
QAngle aim = pawn.EyeAngles;
Vector muzzle = pawn.EyePosition();
bool fullBlind = pawn.FlashMaxAlpha >= 255.0f;

using VoltMod::ObserverMode_t;
if (pawn.GetObserverMode() != ObserverMode_t::Roaming)
    pawn.SetObserverMode(ObserverMode_t::Roaming);
```

Observer mode is a method rather than a field: it lives on a sub-object the pawn points at, so
there is no fixed offset from the pawn to reach it.

## Visibility and the player name

`SetVisible` toggles transparency on the pawn body. Weapons, gloves and grenades stay visible
because they are separate networked entities; for full invisibility use the
@ref sdk_visibility_guide "transmit filter" instead.

```cpp
pawn.SetVisible(false);          // body fully invisible (alpha = 0)
pawn.SetVisible(false, 0x80);    // body 50% transparent
pawn.SetVisible(true);           // restore opaque
```

The scoreboard name is a 128-byte fixed buffer on the controller, wrapped as a
@ref VoltMod::CharBuf. Assignment truncates to 127 characters plus NUL and zeroes the tail;
`Str()` takes an owning copy, `View()` borrows from the buffer.

```cpp
std::string saved = controller.Name.Get().Str();
controller.Name = "";                 // hide on the scoreboard
controller.Name = saved.c_str();      // ... and later, restore
```

Replication piggybacks on the next state-change broadcast, so pair a name write with
`ChangeTeam` or similar when the scoreboard has to refresh immediately.

## PawnOps

Common pawn manipulations, as free functions in `VoltMod::PawnOps`
(`<VoltMod/Entities/PawnOps.hpp>`), plus the engine team constants `TeamNone` / `TeamSpectator` /
`TeamT` / `TeamCT`:

```cpp
namespace PawnOps = VoltMod::PawnOps;

VoltMod::Pawn target = runtime.Entities.PawnOf(slot);

PawnOps::ToggleNoclip(target);              // noclip <-> walk; returns the new on-state
PawnOps::ToggleFreeze(target);              // MoveType None <-> walk
PawnOps::ToggleGodmode(target);             // FL_GODMODE flip (the working CS2 invincibility path)
PawnOps::ShiftZ(target, -15.0f);            // bury; +15 to unbury

// Team lives on the controller, so this one takes that.
PawnOps::ChangeTeamSafe(runtime.Entities.Controller(slot), VoltMod::TeamCT);

// Slap needs framework services for its fall protection, so it lives on runtime.World.Pawns.
runtime.World.Pawns.Slap(target);           // upward punt + timed fall protection

// Teleports: a destination cleared past the anchor's hull, and an exact-origin swap.
Vector dest = PawnOps::ClearedDestination(anchor);   // 48u ahead of anchor's eye yaw
target.Teleport(dest, std::nullopt, Vector{0, 0, 0});
PawnOps::SwapOrigins(a, b);                          // both spots vacate in the same frame
```

`Slap` writes `m_vecAbsVelocity` because teleporting with no origin crashes the
server. Godmode uses `FL_GODMODE` because the legacy `m_takedamage` write has no
effect. Slap fall protection is cancelled on slot changes so it cannot affect a
new occupant.

## Items

@ref VoltMod::Items gives and strips weapons through the pawn's `CCSPlayer_ItemServices`. Both
operations are vtable calls whose indices live in gamedata (`GiveNamedItem`, `RemoveAllItems`), so
a game update is a gamedata edit rather than a rebuild.

```cpp
VoltMod::Pawn target = runtime.Entities.PawnOf(slot);

runtime.World.Items.Give(target, "weapon_ak47");   // entity classname, not a display name
runtime.World.Items.StripWeapons(target);          // pass false to keep armor and the defuse kit
```

`Give` returns false only when the pawn is unavailable or the engine refused the item twice. The
retry matters: the engine rejects a weapon the player's team cannot buy, so a refusal is tried
again with the pawn briefly flipped to the other team and flipped back before this returns. The
flip does mean the call must not be interleaved with anything else that reads the pawn's team.

Anything that fails to resolve - the pawn, the item services pointer, the vtable index - degrades
the call to `false`; it never crashes.
