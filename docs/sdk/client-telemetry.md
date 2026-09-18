# Client telemetry {#sdk_client_telemetry_guide}

[TOC]

## Clock

`runtime.Clock` reads the engine's simulation globals on every call and holds no state.

```cpp
const int tick = runtime.Clock.Tick();    // globals->tickcount
const float now = runtime.Clock.Time();   // globals->curtime, seconds
```

Use it for anything that must line up with the tick the engine is simulating: usercmds, game events,
teleports. Wall time does not stay aligned with engine ticks.

Both readings reset when a map starts, so a value persisted across a map change compares as absurdly
far in the future - drop stamps at map start. Both return `0` when the globals are unavailable
(before load, after shutdown), where `Globals()` is `nullptr`, so `0` reads as "unknown" rather than
"the beginning of the map".

## NetChannels

`runtime.World.NetChannels` reads the engine's per-client channel live. Bots, empty slots and
clients already torn down have no channel, and every accessor degrades instead of asserting.

```cpp
auto& net = runtime.World.NetChannels;

const float rtt = net.EngineLatency(slot);                 // seconds, 0 when unavailable
std::string keep{net.GetUserInfoCvar(slot, "sensitivity")}; // copy before the next engine call
```

`EngineLatency` returns `0` for "no channel", which on a listen server is indistinguishable from a
genuine zero RTT, so pair it with `GetNetInfo(slot) != nullptr` when the difference matters.

`GetUserInfoCvar` only sees convars the client replicates (`FCVAR_USERINFO`: `name`, `sensitivity`,
`m_yaw`, `cl_interp_ratio`, ...). The returned view borrows the engine's own buffer, which the next
userinfo update replaces. Everything outside that set needs a convar query.

## ClientConVars

`runtime.Hooks.ClientConVars` asks one connected client what its own convar is set to. The server
sends `CSVCMsg_GetCvarValue` with a cookie and the client answers later with
`CCLCMsg_RespondCvarValue`, intercepted through a vtable hook on `CServerSideClient`. Queries are
asynchronous, unordered, and may never complete.

```cpp
runtime.Hooks.ClientConVars.Query(slot, "cl_interp_ratio",
    [](int slot, VoltMod::ClientConVarStatus status, std::string_view name, std::string_view value) {
        if (status == VoltMod::ClientConVarStatus::Answered)
            Log::Info("{} answered {} = {}", slot, name, value);
    });
```

`ClientConVarStatus` mirrors the protocol status code: `Answered` (the only case carrying a value),
`NotFound`, `NotAConVar`, `Protected`.

`Query` returns `false` for a bot or empty slot, when the per-slot pending cap is reached, when the
message could not be sent, and whenever the service is not `Available()`.

There is no timeout callback. A client does not have to answer; pending queries expire silently
after 10 seconds, and disconnects produce no callback. Features that need a timeout verdict track
their own deadline. Pending queries are also dropped without firing when the player disconnects,
when a new player takes the slot, and at map start.

Repeating an in-flight query for the same slot and convar re-targets the outstanding request instead
of sending a second one, so polling cannot flood a client; only the newest callback receives the
answer. Distinct convars queue up to `MaxPendingPerSlot` (11) per slot.

`name` and `value` borrow the decoded message and are valid only for the duration of the call, so
copy what you keep; `value` is empty unless `status` is `Answered`. Responses are client-controlled.
The service drops unknown status codes, names that do not match the request cookie, and values with
embedded NULs before invoking the callback - treat what is left as untrusted client input and as
evidence, not proof.

### Availability

`Runtime::Initialize` runs this as an optional load step, because queries have no event subscription that
could install the hook lazily. It needs two gamedata values plus an RTTI or symbol lookup of the
`CServerSideClient` vtable:

| Gamedata entry | What it is | If it drifts |
| --- | --- | --- |
| `CServerSideClient::ProcessRespondCvarValue` | vtable index of the response handler | The step stays unavailable rather than hooking an unrelated vfunc |
| `CServerSideClientBase::m_nClientSlot` | byte offset of the player slot inside `CServerSideClient` | The step stays unavailable rather than attributing answers to the wrong player |

A failure logs one warning and the load continues, so check the status once at load:

```cpp
if (auto available = runtime.Hooks.ClientConVars.Available(); !available)
    Log::Warn("no client convar queries: {}", available.error().Detail);
```

Both values drift with engine updates; see @ref sdk_gamedata_guide.
