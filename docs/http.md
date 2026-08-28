# HTTP {#http_guide}

[TOC]

`VoltMod/Http/` provides one asynchronous client with game-thread completions:

- `HttpClient::Send` is the unified request contract. `Get`, `Post`, `Put`, `Patch`, and `Delete` are convenience helpers over it.
- Requests run on bounded workers, and completions replay on the game thread through self-registered per-frame delivery.

`HttpClient` is a framework service; reach it through `runtime.Http`. The
`Runtime` destructor drains in-flight requests, so the plugin has no separate
shutdown step.

## Requests

```cpp
#include <VoltMod/Api.hpp>

runtime.Http.Post(
    url, body,
    [](const VoltMod::HttpResult& result) {
        // Game thread: safe to touch players, menus, managers.
        if (!result.Ok)
            Log::Warn("request failed: {}", result.Error);
    },
    {"Content-Type: application/json"}, 8000);
```

`HttpResult::Ok` reflects transport success only - a 404 still answered. `HttpResult::IsSuccess()`
is the verdict most callers want: transport succeeded *and* the status is 2xx.

For a request shape the helpers do not cover, fill in an `HttpRequest` and hand it to `Send`.
`AddHeader` writes the `"Key: Value"` line the client parses back, and `AddAuth` assembles a
credential header - an empty scheme sends the key verbatim, and an empty key adds nothing, so an
endpoint configured without one stays unauthenticated:

```cpp
VoltMod::HttpRequest request{
    .Method = VoltMod::HttpMethod::Patch,
    .Url = url,
    .Body = body.dump(),
    .TimeoutMs = cfg.timeoutMs,
};
request.AddHeader("Content-Type", "application/json");
request.AddAuth(cfg.authHeader, cfg.authScheme, cfg.apiKey);

runtime.Http.Send(std::move(request), [](const VoltMod::HttpResult& result) {
    if (!result.IsSuccess())
        return;
});
```

## Threading

Requests run off-thread, but **callbacks never run concurrently with game code**.
Do not block on a request from the game thread; the API is asynchronous by
design.
