# Workshop addons {#workshop_guide}

[TOC]

@ref VoltMod::Addons requires Steam Workshop content for connecting clients,
such as Panorama layouts, models, or sounds.

```cpp
auto required = runtime.Addons.Require(3401234567); // of everyone
if (!required)
    return false;                                   // no dedicated server, or the hook is off

_addon = std::move(*required);                      // required until this Subscription drops

_subs.Add(runtime.Addons.Downloaded += [](int slot) {
    // this client has every addon this plugin requires
});
```

The returned Subscription owns the requirement. Keep it beside the feature that needs
the content. Requirements are reference counted, and `RequireFor(steamId, id)`
adds a player-specific requirement.

Requirements take effect on a client's next connect; already-connected players
are not disturbed. `Require` returns an error when the capability is off or the
server is a listen server, so the plugin can report the reason.

## Building the addon's content

`voltmod panorama compile [OWNER...] --addon NAME --no-deploy` compiles the
screens into `game/csgo_addons/NAME/`, the folder the Workshop Manager uploads.
See @ref panorama_guide_publish for the steps.

## How it works

CS2 handles one addon per connection cycle. @ref VoltMod::Addons rewrites each
join message (`CNETMsg_SignonState`) with the next missing addon. After the final
reconnect, the client joins normally and @ref VoltMod::Addons::Downloaded fires.

Each addon costs the joining client one reconnect, including the first:
the server's own addon string is left alone, and the extras only ride the join
messages.

Addons cannot be batched. The field is a comma-separated list and the engine
puts several entries in it when the server mounts more than one. A client
handles exactly one addon per connection cycle and stalls without downloading
when it receives several. The same behavior is what
[MultiAddonManager](https://github.com/Source2ZE/MultiAddonManager) works around.
VoltMod reduces such a message to its first addon and counts that one as sending, so the
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

## Several plugins

The framework is a static library, so each plugin has its own `Runtime`, its own
@ref VoltMod::Addons and its own hook on the same message. Metamod runs those
hooks one after another on that message, and a hook leaves alone a message an
earlier one already pointed at an addon, counting that addon as sending instead.
A client owing addons to two plugins gets one plugin's, reconnects, then gets the
other's: one addon per reconnect, the same as one plugin requiring both.

Each plugin's @ref VoltMod::Addons::Downloaded, @ref VoltMod::Addons::Missing,
`DownloadTimeoutSeconds` and `MaxDownloadAttempts` cover its own requirements. A
plugin can see its addon arrive while the client still has another plugin's
download, and that reconnect, ahead of it; `Downloaded` fires again after it.

## Availability

Does nothing on a listen server - there is no download step - and when
@ref VoltMod::Capability::Addons is off, which means the
`CServerSideClient::SendNetMessage` vtable entry or one of the two client offsets
did not bind. Either way @ref VoltMod::Addons::Require returns
`ErrorCode::Unsupported` with the reason. @ref VoltMod::Addons::Missing reports
what a connected client still owes.
