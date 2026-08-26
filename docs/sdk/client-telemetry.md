# Client telemetry {#sdk_client_telemetry_guide}

[TOC]

This page covers the simulation clock, per-client network data, and client
convar queries.

## Clock

`runtime.Clock` holds no state and reads `IVEngineServer2::GetServerGlobals()` on every call:

```cpp
const int tick = runtime.Clock.Tick();    // globals->tickcount
const float now = runtime.Clock.Time();   // globals->curtime, seconds
```

A class that needs the clock takes `Engine::Clock&` in its constructor; a free helper takes it as a parameter.

This is the timestamp source for anything that has to line up with the tick the engine is simulating: usercmds, game events, and teleports. Use it instead of `std::chrono`: it is the *simulation* clock, so it stays in step with the tick stream those things are numbered by, which wall time does not.

Two consequences to respect:

- Both **reset when a map starts**. A value persisted across a map change compares as absurdly far in the future; drop stamps at map start rather than carrying them (this is what @ref VoltMod::Teleport "Teleport" does).
- Both return `0` when the globals are unavailable (before load, after shutdown), and `Globals()` returns `nullptr` there. `0` therefore reads as "unknown", not "the beginning of the map".

## NetChannels

`runtime.World.NetChannels` is a stateless wrapper over the engine's per-client channel. Bots, empty slots, and clients whose channel is already torn down simply have no channel; every accessor degrades rather than asserting.

```cpp
auto& net = runtime.World.NetChannels;

const float rtt = net.EngineLatency(slot);                     // seconds, 0 when unavailable
if (const char* sens = net.GetUserInfoCvar(slot, "sensitivity"))
    std::string keep = sens;                                   // copy before the next engine call
```

- `EngineLatency` returns `0` for "no channel", which is indistinguishable from a genuine zero RTT on a listen server, so pair it with `GetNetInfo(slot) != nullptr` when the difference matters.
- `GetUserInfoCvar` only sees cvars the client *replicates* (`FCVAR_USERINFO`: `name`, `sensitivity`, `m_yaw`, `cl_interp_ratio`, ...). The returned string is engine-owned and valid only until the next engine call, so copy anything you keep. Everything outside that set needs a cvar query.

## ClientCvars

`runtime.Hooks.ClientCvars` asks a connected client what one of *its* convars is set to. The server posts `CSVCMsg_GetCvarValue` carrying a cookie to that one client; the client answers with `CCLCMsg_RespondCvarValue` some round-trips later. The framework intercepts the answer with a manual **DVP** hook on `CServerSideClient::ProcessRespondCvarValue` (bound to the class vtable, so it covers every connected client without per-instance bindings), reads the responder's slot from a gamedata byte offset, and routes the value to the callback that asked for it. The engine's own handling of the response is untouched (`MRES_IGNORED`).

```cpp
runtime.Hooks.ClientCvars.Query(slot, "cl_interp_ratio",
    [](int slot, VoltMod::ClientCvarStatus status, std::string_view name, std::string_view value) {
        if (status == VoltMod::ClientCvarStatus::ValueIntact)
            Log::Info("{} answered {} = {}", slot, name, value);
    });
```

`ClientCvarStatus` mirrors the protocol's status code: `ValueIntact` (the only case carrying a value), `CvarNotFound`, `NotACvar`, `CvarProtected`. `VoltMod::Name(status)` gives the enumerator's identifier for logs.

### No timeout callback

A client does not have to answer. Pending entries expire silently after 10
seconds, and disconnects also produce no callback. If a feature needs a timeout
verdict, track its own deadline rather than waiting for this callback.

Pending queries are also dropped, with their callbacks never firing, when the player disconnects, when a new player takes the slot, and at map start.

### Re-query and the pending cap

Querying a convar that is **already in flight for that slot** re-targets the outstanding request instead of sending a second one, so polling the same convar cannot flood a client. The new callback *replaces* the previous one: only one answer arrives, and only the latest callback sees it.

Distinct convars queue up to `MaxPendingPerSlot` (11) outstanding per slot; past that `Query()` returns false rather than piling requests on a silent client. It also returns false for bots and empty slots (no net channel), and whenever the service is unavailable.

### Borrowed strings and trust

`name` and `value` borrow the decoded message and are valid **only for the duration of the call**, so copy what you keep. `value` is empty unless `status` is `ValueIntact`.

Responses are client-controlled, so the service drops anything malformed before your callback runs: unknown status codes, a name that does not match what that cookie asked for, and values containing embedded NULs (which would truncate anywhere they are treated as a C string). What survives is still a value a modified client chose to send, so treat it as evidence rather than proof.

### Availability and gamedata drift

The service is a **degradable load stage** (`ClientCvars`). It needs two gamedata offsets and an RTTI/symbol lookup of the `CServerSideClient` vtable:

| Offset | What it is | If it drifts |
|--------|------------|--------------|
| `ProcessRespondCvarValue` | vtable index of the response handler | Rejected at lookup, so the stage degrades instead of hooking an unrelated vfunc |
| `ServerSideClientSlot` | byte offset of the player slot inside `CServerSideClient` | Rejected at lookup too; unchecked it would attribute answers to the wrong player |

Both drift with engine updates; see @ref sdk_gamedata_guide. When any part of the setup fails the framework logs one warning, the load continues, `Capability::ClientCvars` is off and carries the reason, and every `Query()` returns false. Check the capability once at load rather than treating each `false` from `Query()` as a per-call failure:

```cpp
if (!runtime.Capabilities.Has(VoltMod::Capability::ClientCvars))
    Log::Warn("no client convar queries: {}", runtime.Capabilities.Reason(VoltMod::Capability::ClientCvars));
```

Unlike the movement, damage and teleport hooks, this one is not lazily installed: it is a load stage, because the service is a query API with no event of its own to subscribe to. `Runtime::Start` installs it and records the outcome; a successful install logs `Client convar response hook installed on CServerSideClient vtable (index N).` followed by `Client convar queries enabled (slot offset N).`
