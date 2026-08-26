# Entities and players {#sdk_players_guide}

[TOC]

## The object model

Three value types wrap a live entity, each adding to the one before it:

| Type | What it is | Where it comes from |
| ---- | ---------- | ------------------- |
| @ref VoltMod::Entity | Any entity: the CBaseEntity fields, position, teleport | `runtime.Entities.Resolve(ref)`, `FindByClassName`, `FindByName` |
| @ref VoltMod::Pawn | A player's body: health, armor, movement, aim, render | `runtime.Entities.PawnOf(slot)` |
| @ref VoltMod::Controller | A player's identity: name, money, team, kick | `runtime.Entities.Controller(slot)` |

The split matters. The controller is the scoreboard row and survives death, team changes and
respawns; the pawn is the body and is replaced on every spawn. **A player's health is on the pawn**
(`controller.GetPawn().Health`, or just `runtime.Entities.PawnOf(slot).Health`). The CBaseEntity
fields a `Controller` inherits are the controller entity's own and mean nothing for gameplay.

```cpp
auto pawn = runtime.Entities.PawnOf(slot);
if (!pawn)
    return;

int hp = pawn.Health;          // reads m_iHealth
pawn.Health = 100;             // writes it *and* dirties it for replication
pawn.Health += 25;             // += -= |= &= all work

auto controller = runtime.Entities.Controller(slot);
controller.Kick("Cheating");
controller.ChangeTeam(VoltMod::TeamCT);
```

## Validity, and never storing a wrapper

Every wrapper is a **frame-local value**. It holds a raw entity pointer, and the engine frees
entities between frames without telling anyone.

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

Reading a field of a falsy wrapper yields a zero value and writing it does nothing, so a wrapper
that never resolved degrades rather than crashing. A stale non-null one does not - which is why
the rule is "never store one" and not "check before use".

## Fields

A member declared as @ref VoltMod::Field is a schema field used as if it were a data member. It
converts to its type on read, takes its type on write, and compares directly:

```cpp
if (pawn.Team == VoltMod::TeamCT && pawn.Flags.Get() & VoltMod::FL_ONGROUND)
    pawn.SpeedModifier = 1.5f;
```

Three things a `Field` does that a raw offset write does not:

1. **It resolves itself, once per process.** No service to thread through, and the offset is
   looked up once per `(class, field)` for the lifetime of the server, not once per read.
2. **It walks base classes.** `m_angEyeAngles` is declared on `CCSPlayerPawnBase`; asking for it
   on `CCSPlayerPawn` finds it, and the other way round.
3. **It replicates.** A write to a networked field dirties it, so the client sees the new value on
   the next snapshot instead of whenever something else happens to touch the entity.

`Field::Ref()` exposes the resolved @ref VoltMod::FieldRef (offset, size, networked, chain), and
`Get()` / `Set()` are there when the conversion operators read badly at a call site.

For a field with no wrapper to hang it on, declare a `static` @ref VoltMod::LazyField - see
@ref sdk_gamedata_guide "Gamedata".

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

// Slap needs framework services for its fall protection, so it lives on runtime.Pawns.
runtime.Pawns.Slap(target);                 // upward punt + timed fall protection

// Teleports: a destination cleared past the anchor's hull, and an exact-origin swap.
Vector dest = PawnOps::ClearedDestination(anchor);   // 48u ahead of anchor's eye yaw
target.Teleport(dest, std::nullopt, Vector{0, 0, 0});
PawnOps::SwapOrigins(a, b);                          // both spots vacate in the same frame
```

Two CS2 workarounds are baked in: `Slap` writes velocity through `m_vecAbsVelocity` because
`Teleport(nullopt origin, ...)` crashes the server, and godmode uses the `FL_GODMODE` flag because
the legacy `m_takedamage` write is a no-op. `Slap`'s fall protection clears itself on a scheduler
timer, and the `SlotEvents` listener it takes cancels that timer if the player leaves first, so
the next occupant of the slot never has godmode stripped.

## Items

@ref VoltMod::Items gives and strips weapons through the pawn's `CCSPlayer_ItemServices`. Both
operations are vtable calls whose indices live in gamedata (`GiveNamedItem`, `RemoveAllItems`), so
a game update is a gamedata edit rather than a rebuild.

```cpp
VoltMod::Pawn target = runtime.Entities.PawnOf(slot);

runtime.Items.Give(target, "weapon_ak47");   // entity classname, not a display name
runtime.Items.StripWeapons(target);          // pass false to keep armor and the defuse kit
```

`Give` returns false only when the pawn is unavailable or the engine refused the item twice. The
retry matters: the engine rejects a weapon the player's team cannot buy, so a refusal is tried
again with the pawn briefly flipped to the other team and flipped back before this returns. The
flip does mean the call must not be interleaved with anything else that reads the pawn's team.

Anything that fails to resolve - the pawn, the item services pointer, the vtable index - degrades
the call to `false`; it never crashes.
