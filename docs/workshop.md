# Workshop addons {#workshop_guide}

[TOC]

@ref VoltMod::Addons makes connecting clients download Steam Workshop content: Panorama layouts,
models, sounds. The server mounts nothing.

```cpp
auto required = runtime.Addons.Require(3401234567);
if (!required)
    return false;                                   // listen server, or the hooks did not bind

_addon = std::move(*required);                      // required until this Subscription drops

_subs.Add(runtime.Addons.Downloaded += [](int slot) {
    // this client has every addon this plugin requires
});
```

The Subscription owns the requirement, so keep it beside the feature that needs the content.
Requirements are reference counted. `RequireFor(steamId, id)` requires an addon of one player
only. `Required()` lists what every client must have, `Missing(slot)` what one client still owes,
and `HasMissing(slot)` answers the same question without building the list.

Requirements take effect on a client's next connect; already-connected players are not disturbed.
`Require` returns `ErrorCode::Invalid` for id 0 and `ErrorCode::Unsupported` on a listen server or
when the hooks could not install, with the reason in `Error::Detail`.

## Building the content

`voltmod panorama compile [OWNER...] --addon NAME --no-deploy` compiles the screens into
`game/csgo_addons/NAME/`, the folder the Workshop Manager uploads. See @ref panorama_guide_publish.

## One addon per reconnect

Two engine messages carry an addon and a client needs both:

- The join message (`CNETMsg_SignonState`) sends the client away to download an addon and
  reconnect. @ref VoltMod::Addons rewrites it with the next missing addon.
- The connection reply (`CNetworkGameServer::ReplyConnection`) names the addons the client mounts
  for the session. The reply copies the server's own addon list, so @ref VoltMod::Addons appends
  this client's downloaded addons plus the one being downloaded, then removes its additions.

A client that downloaded an addon but was not told to mount it has the files and no content: a
menu drawn on that layout is invisible.

The join message's field is a comma-separated list, but a client handles exactly one addon per
connection cycle and stalls without downloading when it receives several. The framework reduces
such a message to its first addon, so the client makes progress. Each addon therefore costs the
joining client one reconnect, including the first.

The server gets no download-complete signal. A reconnect within `Addons::DownloadTimeoutSeconds`
(30 by default) counts as success; a later one retries the addon. The same addon is offered at
most `Addons::MaxDownloadAttempts` times (3) before a declining client is dropped, which stops an
endless reconnect loop. Progress is keyed by SteamID, because a client cycling through downloads
changes slots.

Each plugin has its own @ref VoltMod::Addons and its own hooks on these messages, and they run one
after another. A join-message hook leaves alone a message an earlier one already pointed at an
addon, and each plugin takes back only the reply entries it appended, so the reply names every
plugin's addons whichever order the hooks run in. A client owing addons to two plugins gets one
plugin's, reconnects, then gets the other's.

## Server-side content

If the server itself needs the content - a custom map, models server-side code touches - install
and mount it the usual way; a workshop map still goes through @ref VoltMod::Map::ChangeToWorkshop.
Server-side downloading needs `ISteamUGC`, which the SDK does not link.

## Availability

The service is inactive on a listen server, where there is no download step, and when the
`CServerSideClient::SendNetMessage` vtable entry, the `CNetworkGameServer::ReplyConnection`
signature, or the client and server offsets they read did not bind. Either way `Require` returns
`ErrorCode::Unsupported` with the reason.
