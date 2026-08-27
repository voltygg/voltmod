# Workshop addons {#workshop_guide}

[TOC]

@ref VoltMod::Addons tells connecting clients which Steam Workshop addons to
download. Use it for content only the client renders - Panorama layouts (see
@ref custom_hud_guide), models, sounds.

```cpp
runtime.Addons.Require(3401234567);                 // of everyone
runtime.Addons.RequireFor(steamId, 3409999999);     // of one player as well

_subs.push_back(runtime.Addons.Ready += [](int slot) {
    // this client has everything and is joining normally
});
```

Requirements take effect on a client's next connect; already-connected players
are not disturbed.

## How it works

CS2 hands a client one addon per connection cycle. Each time the server sends a
client its signon message, @ref VoltMod::Addons rewrites the message to name the
next addon that client is still missing, which makes the client download it and
reconnect. When the list runs out the client joins normally and @ref
VoltMod::Addons::Ready fires.

So **each addon costs the joining client one reconnect**, including the first:
the server's own addon string is left alone, and the extras only ride the signon
cycle.

## What it does not do

Nothing is downloaded or mounted **on the server**. If the server itself needs
the content - a custom map, models the server-side code touches - install and
mount it the usual way; a workshop map still goes through
@ref VoltMod::Map::ChangeToWorkshop.

This is deliberate. Server-side downloading needs `ISteamUGC`, which the SDK does
not link, and the handshake rewrite that would save the first reconnect needs
inline detours, which the framework does not have. The subset here is the one
that works with vtable hooks alone, and it is the same subset
[MultiAddonManager](https://github.com/Source2ZE/MultiAddonManager) exposes as
`mm_client_extra_addons`.

## The one guess it makes

Nothing tells the server that a download finished. A client is taken to have got
the addon it was last sent if it reconnects within
`Addons::DownloadTimeoutSeconds` (30s by default); a client returning later than
that starts that addon over. Raise it for large addons or slow connections.

A client that declines the download would otherwise reconnect forever, so the
same addon is offered at most `Addons::MaxDownloadAttempts` times (3) before that
client is dropped.

Progress is keyed by SteamID, not slot, because a client cycling through
downloads reconnects and its slot changes.

## Availability

Inert on a listen server - there is no download step - and when
@ref VoltMod::Capability::Addons is off, which means the
`CServerSideClient::SendNetMessage` vtable entry or one of the two client offsets
did not bind. `Addons::Pending(slot)` reports what a connected client still owes.
