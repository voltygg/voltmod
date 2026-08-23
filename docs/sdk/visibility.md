# Rendering & Visibility {#sdk_visibility_guide}

[TOC]

## EntityRender

Mutate `m_nRenderMode` and `m_clrRender` on any `CBaseModelEntity`. Used internally by `PlayerController::SetVisible`, but exposed so plugins can hide/recolor any entity (props, dropped weapons, world objects).

```cpp
using namespace CS2Kit::Sdk;

SetEntityRender(prop, RenderMode_t::TransTexture, ColorInvisible);
SetEntityRender(prop, RenderMode_t::Normal, ColorOpaqueWhite);
```

`m_clrRender` is RGBA packed as `(A << 24) | (B << 16) | (G << 8) | R` - `ColorInvisible` (`0x00FFFFFF`) is white at zero alpha.

Render tricks only affect the pawn body; held weapons, wearables and gloves are separate networked entities. For true invisibility use the TransmitFilter instead.

## TransmitFilter

Per-recipient entity transmit filtering via a post-hook on `ISource2GameEntities::CheckTransmit`. Hidden entities are never sent to the client, so the model, weapons, wearables, gloves and shadow all disappear - unlike render alpha. Two independent per-slot toggles:

```cpp
auto& transmit = runtime.Transmit;

transmit.SetPawnHidden(slot, true);        // pawn + weapons + wearables vanish for everyone else
transmit.SetControllerHidden(slot, true);  // removes the player's scoreboard row
```

The hidden player still receives their own entities, and a client actively observing the hidden pawn keeps receiving it (dropping it would break the spectator camera). Sounds (footsteps, gunfire) are networked separately and are not filtered. State is cleared automatically when the player disconnects.

Arbitrary entities can also be made exclusive to a single client - the building block for per-viewer effects like GlowVision below:

```cpp
transmit.SetEntityExclusive(entityIndex, beneficiarySlot);  // only this client receives it
transmit.ClearEntityExclusive(entityIndex);                 // transmits normally again
```

Clear the registration *before* removing the entity: a recycled index still registered would filter whatever entity the engine hands that index to next.

Requires the `CheckTransmitPlayerSlot` gamedata offset (the recipient slot inside the partially-reversed `CCheckTransmitInfo`); if it is missing the service logs a warning at load and becomes inert.

## GlowVision

Per-viewer wallhack-style vision built on the TransmitFilter: one client sees live players as team-colored glow outlines through walls, while every other client (and GOTV) never receives the glow entities - invisible to them by construction, not by rendering tricks. Each glowing player gets two `prop_dynamic` clones following their pawn - an invisible relay and a glow prop parented to it (the indirection renders only the outline) - both transmit-filtered exclusively to the beneficiary.

```cpp
using CS2Kit::Sdk::GlowVision;

auto glow = std::make_shared<GlowVision>(viewerSlot);
glow->Reconcile();  // build the clones immediately

// Then drive it from a repeating tick, e.g. an EffectManager spec:
//   .TickIntervalMs = GlowVision::ReconcileIntervalMs,
//   .OnTick = [glow] { glow->Reconcile(); },
//   .OnStop = [glow] { glow->Destroy(); },
```

`Reconcile` tracks spawns, deaths, and team/model changes, and rebuilds clones the engine destroyed on a round restart. It skips the beneficiary, dead and spectating players, and pawns hidden via the TransmitFilter (a ghosted pawn never transmits, so a clone would follow nothing). `Destroy` clears the transmit-filter entries and removes any surviving clones.

Team colors and the glow set are configurable; the optional `Filter` veto runs on top of the built-in checks:

```cpp
GlowVision::Config config{
    .TerroristColor = Color(255, 0, 0, 255),
    .CtColor = Color(0, 255, 0, 255),
    .Filter = [](int slot) { return PlayerController(slot).GetTeam() == TeamT; },  // Ts only
};
auto glow = std::make_shared<GlowVision>(viewerSlot, std::move(config));
```

Costs two entities per glowing player and inherits the TransmitFilter's gamedata requirement - without the `CheckTransmitPlayerSlot` offset the clones would be visible to everyone, so do not use it when the filter is inert.
