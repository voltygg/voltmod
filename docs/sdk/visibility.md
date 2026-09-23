# Rendering and visibility {#sdk_visibility_guide}

[TOC]

## Render

`Entity::SetRender` writes `m_nRenderMode` and `m_clrRender` and dirties both for replication. It
works on any model entity: a prop, a weapon, a pawn. The mode is the generated
`Schema::RenderMode_t`, CS2's own numbering: `kRenderNormal`, `kRenderTransAlpha` (the color's alpha
applies) and `kRenderNone`.

```cpp
using VoltMod::Schema::RenderMode_t;

prop.SetRender(RenderMode_t::kRenderTransAlpha, VoltMod::Color{.A = 0});
prop.SetRender(RenderMode_t::kRenderNormal, VoltMod::Color{});

runtime.Entities.Pawn(slot).SetVisible(false);        // the pawn body, alpha 0
runtime.Entities.Pawn(slot).SetVisible(false, 0x80);  // 50% transparent
```

`VoltMod::Color` holds the RGBA bytes in the engine's order. Render tricks reach only the
pawn body: held weapons, wearables and gloves are separate networked entities that CS2 routes
through systems a server plugin cannot touch. For real invisibility use the visibility filter.

## Visibility

@ref VoltMod::Visibility filters `ISource2GameEntities::CheckTransmit`, so a hidden pawn and the
entities that follow it are never sent to other clients.

```cpp
auto& visibility = runtime.Hooks.Visibility;

visibility.SetPawnHidden(slot, true);        // pawn, weapons, wearables, gloves and shadow
visibility.SetControllerHidden(slot, true);  // removes the scoreboard row
```

The hidden player still receives their own entities, and a client actively observing the hidden pawn
keeps receiving it so the spectator camera does not break. Sounds are networked separately and are
not filtered. State clears when the slot changes hands.

Any entity can also be networked to a single client, which is what per-viewer effects are built on:

```cpp
visibility.ShowOnlyTo(entity.Ref(), viewerSlot);  // only this client receives it
visibility.ShowToEveryone(entity.Ref());          // networked normally again
```

Entries are keyed by @ref VoltMod::EntityRef, so an entry whose entity is gone drops itself and
removing the entity is enough. `ScreenManager::ForPlayer` is built on the same mechanism.

`Available()` fails when the `CheckTransmitPlayerSlot` gamedata offset - the recipient slot inside
the partially reversed `CCheckTransmitInfo` - did not bind. Every call above is then accepted but
inert, which for a hide means the player stays visible.

## GlowVision

@ref VoltMod::GlowVision builds per-viewer outlines on the visibility filter: one client sees live
players as team-colored glows through walls, and no other client or GOTV ever receives the helper
entities. Each glowing player costs two `prop_dynamic` clones.

```cpp
auto glow = runtime.Hooks.Visibility.CreateGlow(viewerSlot);
glow->Refresh();  // build the clones now

// Then drive it from a repeating tick:
//   .TickIntervalMs = VoltMod::GlowVision::RefreshIntervalMs,
//   .OnTick = [glow] { glow->Refresh(); },
//   .OnStop = [glow] { glow->Destroy(); },
```

`Refresh` tracks spawns, deaths and team or model changes, and rebuilds clones the engine destroyed
on a round restart. It skips the viewer, dead and spectating players, and pawns hidden by the
visibility filter, since a ghosted pawn never transmits and its clone would follow nothing.
`Destroy` clears the filter entries and removes any surviving clones.

Colors and the optional per-slot veto run on top of the built-in checks:

```cpp
VoltMod::GlowConfig config{
    .TerroristColor = VoltMod::Color{255, 0, 0},
    .CtColor = VoltMod::Color{0, 255, 0},
    .Filter = [&runtime](int slot) { return runtime.Entities.Controller(slot).Team() == VoltMod::Team::T; },
};
auto glow = runtime.Hooks.Visibility.CreateGlow(viewerSlot, std::move(config));
```

Without the `CheckTransmitPlayerSlot` offset the clones would be visible to everyone, so do not use
glow when the filter is inert.
