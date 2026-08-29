# HTTP {#http_guide}

[TOC]

`runtime.Http` is an asynchronous client whose callbacks run on the game thread:

- `HttpClient::Send` is the unified request contract. `Get`, `Post`, `Put`, `Patch`, and `Delete` are convenience helpers over it.
- Requests run on bounded workers, and completions replay on the game thread through self-registered per-frame delivery.

The runtime drains in-flight requests during shutdown; plugins need no separate
HTTP cleanup.

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

For other request shapes, construct `HttpRequest` and call `Send`. `AddHeader`
formats a header line. `AddAuth` omits authentication for an empty key and sends
the key verbatim when the scheme is empty:

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
