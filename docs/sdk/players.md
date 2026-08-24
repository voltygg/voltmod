# Entities & Players {#sdk_players_guide}

[TOC]

## EntitySystem

Access player controllers and entity data:

```cpp
auto& es = runtime.Entities;

// Get the raw controller entity by slot
auto* controller = es.GetPlayerController(slot);

// Check if a slot is valid
bool valid = es.IsPlayerSlotValid(slot);

// Read player button state
uint64_t buttons = es.GetPlayerButtons(slot);

// Iterate map entities (signature-backed; both return nullptr when exhausted)
CEntityInstance* door = nullptr;
while ((door = es.FindByClassName(door, "func_door")))
    /* ... */;
auto* named = es.FindByName(nullptr, "my_targetname");
```

For typed operations on a player, construct a @ref VoltMod::Sdk::PlayerController from the slot (see
below) rather than working with the raw `CEntityInstance*`.

## PlayerController

Typed wrapper around `CCSPlayerController` for common operations. Construct it from a player
slot - it resolves the controller entity internally (check `IsValid()` if the slot may be
empty). When you already hold a tracked `VoltMod::Player*`, `player->Controller()` builds the
same wrapper:

```cpp
VoltMod::PlayerController player(slot);   // or: trackedPlayer->Controller()

int health = player.GetHealth();
int team = player.GetTeam();

player.Kick("Cheating");
player.Slay();
player.ChangeTeam(3);  // CT
player.Respawn();
```

### Aim & flash state

For angle/position analysis (anti-cheat, aim telemetry): `GetEyeAngles()` reads the pawn's
networked eye angles, `GetEyePosition()` is the abs origin plus `m_vecViewOffset` - where the
player's shots originate. `GetFlashDuration()` / `GetFlashMaxAlpha()` expose the flashbang
state the last `player_blind` set on the pawn (`m_flFlashDuration`, `m_flFlashMaxAlpha`;
255 max-alpha means a full blind). For blind-time bookkeeping, prefer the typed
`Events::PlayerBlind` event, which carries `BlindDuration` directly.

```cpp
QAngle aim = player.GetEyeAngles();
Vector muzzle = player.GetEyePosition();
bool fullBlind = player.GetFlashMaxAlpha() >= 255.0f;
```

### Visibility

`SetVisible` toggles transparency on the player pawn body. Weapons, gloves, and grenades stay visible - they are separate networked entities that the render fields on the pawn do not reach. For full invisibility use the [TransmitFilter](@ref sdk_visibility_guide) instead.

```cpp
player.SetVisible(false);          // body fully invisible (alpha = 0)
player.SetVisible(false, 0x80);    // body 50% transparent
player.SetVisible(true);           // restore opaque
```

### Observer mode

Read or force a player into a specific spectator mode:

```cpp
using VoltMod::Sdk::ObserverMode_t;

if (player.GetObserverMode() != ObserverMode_t::Roaming)
    player.SetObserverMode(ObserverMode_t::Roaming);
```

### Player name

Read/write `m_iszPlayerName` on the controller. `SetPlayerName` writes into the 128-byte fixed buffer (truncating to 127 chars + NUL); it does **not** trigger a scoreboard re-sync on its own. Replication piggybacks on the next state-change broadcast, so pair it with `ChangeTeam` or similar if you need an immediate refresh:

```cpp
auto saved = player.GetPlayerName();
player.SetPlayerName("");        // hide on scoreboard
// ... later
player.SetPlayerName(saved);
```

## PawnOps

Common pawn manipulations composed from `PlayerController` primitives - the operations most
gameplay plugins end up writing by hand. Free functions in `VoltMod::Sdk::PawnOps`
(`<VoltMod/Sdk/PawnOps.hpp>`), plus the engine team constants `TeamNone` / `TeamSpectator` /
`TeamT` / `TeamCT`:

```cpp
using namespace VoltMod::Sdk;
PlayerController target(slot);

PawnOps::ToggleNoclip(target);              // noclip <-> walk; returns the new on-state
PawnOps::ToggleFreeze(target);              // MoveType None <-> walk
PawnOps::ToggleGodmode(target);             // FL_GODMODE flip (the working CS2 invincibility path)
PawnOps::ShiftZ(target, -15.0f);            // bury; +15 to unbury
PawnOps::Slap(target);                      // upward punt + timed fall protection
PawnOps::ChangeTeamSafe(target, TeamCT);    // bounds-checked ChangeTeam

// Teleports: a destination cleared past the anchor's hull, and an exact-origin swap.
Vector dest = PawnOps::ClearedDestination(anchor);   // 48u ahead of anchor's eye yaw
PawnOps::SwapOrigins(a, b);                          // both spots vacate in the same frame
```

Two CS2 workarounds are baked in: `Slap` writes velocity via `m_vecAbsVelocity` because
`Teleport(nullptr, ...)` crashes the server, and godmode uses the `FL_GODMODE` flag because the
legacy `m_takedamage` write is a no-op.
