# Workshop addons {#workshop_guide}

[TOC]

@ref VoltMod::Addons requires Steam Workshop content for connecting clients,
such as Panorama layouts, models, or sounds.

```cpp
auto required = runtime.Addons.Require(3401234567); // of everyone
if (!required)
    return false;                                   // no dedicated server, or the hooks are off

_addon = std::move(*required);                      // required until this Subscription drops

_subs.Add(runtime.Addons.Downloaded += [](int slot) {
    // this client has every addon this plugin requires
});
```

The returned Subscription owns the requirement. Keep it beside the feature that needs
the content. Requirements are reference counted, and `RequireFor(steamId, id)`
adds a player-specific requirement.

Requirements take effect on a client's next connect; already-connected players
are not disturbed. `Require` returns an error when its bindings are unavailable or
the server is a listen server, so the plugin can report the reason.

## Building the addon's content

`voltmod panorama compile [OWNER...] --addon NAME --no-deploy` compiles the
screens into `game/csgo_addons/NAME/`, the folder the Workshop Manager uploads.
See @ref panorama_guide_publish for the steps.

## How it works

Two engine messages carry an addon, and a client needs both:

- **The join message** (`CNETMsg_SignonState`) sends the client away to download an
  addon and reconnect. @ref VoltMod::Addons rewrites it with the next missing addon.
- **The connection reply** (`CNetworkGameServer::ReplyConnection`) names the addons
  the client mounts for the session. The reply copies the server's own addon list,
  so @ref VoltMod::Addons temporarily appends this client's downloaded addons and
  the addon currently being downloaded, then removes its additions.

A client that downloaded an addon but was not told to mount it has the files and no
content; a menu drawn on that layout is invisible. After the final reconnect the
client joins normally and @ref VoltMod::Addons::Downloaded fires.

Each addon costs the joining client one reconnect, including the first.

Addons cannot be batched. The join message's field is a comma-separated list and the
engine puts several entries in it when the server mounts more than one. A client
handles exactly one addon per connection cycle and stalls without downloading when it
receives several. This is the behavior that
[MultiAddonManager](https://github.com/Source2ZE/MultiAddonManager) works around.
VoltMod reduces such a message to its first addon and counts that one as sending, so the
client makes progress instead of stalling.

## Server-side content

Nothing is downloaded or mounted **on the server**. If the server itself needs
the content - a custom map, models the server-side code touches - install and
mount it the usual way; a workshop map still goes through
@ref VoltMod::Map::ChangeToWorkshop.

Server-side downloading requires `ISteamUGC`, which the SDK does not link. The
available subset is the `mm_client_extra_addons` interface exposed by
[MultiAddonManager](https://github.com/Source2ZE/MultiAddonManager).

## Download completion

The server receives no download-complete signal. It treats a reconnect within
`Addons::DownloadTimeoutSeconds` (30 seconds by default) as success;
later reconnects retry the addon. Increase the timeout for large downloads or
slow clients.

A client that declines the download would otherwise reconnect forever, so the
same addon is offered at most `Addons::MaxDownloadAttempts` times (3) before that
client is dropped.

Progress is keyed by SteamID, not slot, because a client cycling through
downloads reconnects and its slot changes.

## Several plugins

The framework is a static library, so each plugin has its own `Runtime`, its own
@ref VoltMod::Addons and its own hooks on the same messages. Metamod runs those
hooks one after another, and a join-message hook leaves alone a message an earlier
one already pointed at an addon, counting that addon as sending instead. A client
owing addons to two plugins gets one plugin's, reconnects, then gets the other's:
one addon per reconnect, the same as one plugin requiring both.

Each plugin appends its own addons to the connection reply and takes back only
those it appended, so the reply names every plugin's addons whichever order the
hooks run in.

Each plugin's @ref VoltMod::Addons::Downloaded, @ref VoltMod::Addons::Missing,
`DownloadTimeoutSeconds` and `MaxDownloadAttempts` cover its own requirements. A
plugin can see its addon arrive while the client still has another plugin's
download, and that reconnect, ahead of it; `Downloaded` fires again after it.

## Availability

The service is inactive on a listen server, where no download step exists, or when one of these
entries did not bind:
`CServerSideClient::SendNetMessage` vtable entry, the `CNetworkGameServer::ReplyConnection`
signature, or the client and server offsets they read. Either way @ref VoltMod::Addons::Require
returns `ErrorCode::Unsupported` with the reason. @ref VoltMod::Addons::Missing
reports what a connected client still owes.
