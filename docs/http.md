# HTTP {#http_guide}

[TOC]

```cpp
#include <VoltMod/Api.hpp>

runtime.Http.Post(
    url, body,
    [](const VoltMod::HttpResult& result) {
        // Game thread: safe to touch players, menus, managers.
        if (!result.IsSuccess())
            Log::Warn("request failed: {} ({})", result.Error, result.StatusCode);
    },
    {"Content-Type: application/json"}, 8000);
```

`runtime.Http` runs requests off the game thread and replays completions on it, through a
per-frame subscription the client registers for itself. `Get`, `Post`, `Put`, `Patch` and
`Delete` all take `(url[, body], onComplete, headers = {}, timeoutMs = 8000)` and are
conveniences over `Send`. The runtime aborts in-flight requests during shutdown, so a plugin
needs no HTTP cleanup of its own; requests sent after that are dropped and their completions
never run.

@ref VoltMod::HttpResult carries `Ok`, `StatusCode`, `Body` and `Error`. `Ok` is transport
success alone - a 404 answered, so it is `true`. `IsSuccess()` is `Ok` and a 2xx status.

For any other request shape, fill an @ref VoltMod::HttpRequest and call `Send`:

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

`Headers` are full `"Key: Value"` lines; `AddHeader` formats one. `AddAuth(header, scheme, key)`
is a no-op for an empty key, so an endpoint configured without one stays unauthenticated instead
of sending an empty credential, and an empty scheme sends the key verbatim.

Do not block on a request from the game thread. Completions never run concurrently with game code.
