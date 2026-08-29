# Workshop addons {#workshop_guide}

[TOC]

@ref VoltMod::Addons requires Steam Workshop content for connecting clients,
such as Panorama layouts, models, or sounds.

```cpp
auto lease = runtime.Addons.Require(3401234567);    // of everyone
if (!lease)
    return false;                                   // no dedicated server, or the hook is off

_addon = std::move(*lease);                         // required until this Subscription drops

_subs.push_back(runtime.Addons.Ready += [](int slot) {
    // this client has everything and is joining normally
});
```

The returned lease owns the requirement. Keep it beside the feature that needs
the content. Requirements are reference counted, and `RequireFor(steamId, id)`
adds a player-specific requirement.

Requirements take effect on a client's next connect; already-connected players
are not disturbed. `Require` fails rather than quietly doing nothing when the
capability is off or the server is a listen server, so a plugin can say so.

## How it works

CS2 handles one addon per connection cycle. @ref VoltMod::Addons rewrites each
signon message with the next missing addon. After the final reconnect, the client
joins normally and @ref VoltMod::Addons::Ready fires.

So **each addon costs the joining client one reconnect**, including the first:
the server's own addon string is left alone, and the extras only ride the signon
cycle.

They cannot be batched. The field is a comma-separated list and the engine does
put several in it when the server itself mounts more than one, but a client
handles exactly one addon per connection cycle and stalls with none downloaded
when handed several - the same behaviour
[MultiAddonManager](https://github.com/Source2ZE/MultiAddonManager) works around.
VoltMod reduces such a message to its first addon and credits that one, so the
client makes progress instead of stalling.

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

The server receives no download-complete signal. A reconnect within
`Addons::DownloadTimeoutSeconds` (30 seconds by default) counts as success;
later reconnects retry the addon. Increase the timeout for large downloads or
slow clients.

A client that declines the download would otherwise reconnect forever, so the
same addon is offered at most `Addons::MaxDownloadAttempts` times (3) before that
client is dropped.

Progress is keyed by SteamID, not slot, because a client cycling through
downloads reconnects and its slot changes.

## One plugin owns the list

The framework is a static library, so each plugin has its own `Runtime`, its own
@ref VoltMod::Addons and its own hook on the same message. Two plugins requiring
different addons do not merge their lists - each rewrites the message with its
own next id, and whichever hook runs last decides what the client is told.

Since batching is impossible (above), there is no way to serve both at once.
Give one plugin the addon list and let the others ask it, rather than calling
@ref VoltMod::Addons::Require from several plugins on the same server.

## Availability

Inert on a listen server - there is no download step - and when
@ref VoltMod::Capability::Addons is off, which means the
`CServerSideClient::SendNetMessage` vtable entry or one of the two client offsets
did not bind. Either way @ref VoltMod::Addons::Require returns
`ErrorCode::Unsupported` with the reason. @ref VoltMod::Addons::Pending reports
what a connected client still owes.
