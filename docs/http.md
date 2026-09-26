# HTTP {#http_guide}

[TOC]

```cpp
#include <VoltMod/Api.hpp>

VoltMod::HttpRequest request{
    .Method = VoltMod::HttpMethod::Post,
    .Url = url,
    .Body = body.dump(),
    .Headers = {{"Content-Type", "application/json"}},
    .TimeoutMs = cfg.timeoutMs,
};
request.AddAuth(cfg.authHeader, cfg.authScheme, cfg.apiKey);

runtime.Http.Send(std::move(request), [](const VoltMod::HttpResult& result) {
    // Game thread: safe to touch players, menus, managers.
    if (!result.IsSuccess())
        Log::Warn("request failed: {} ({})", result.Error, result.StatusCode);
});
```

`runtime.Http` runs up to four requests at once off the game thread and runs each completion on
the game thread on a later frame. `TimeoutMs` defaults to 8000.

@ref VoltMod::HttpResult carries `Ok`, `StatusCode`, `Body` and `Error`. `Ok` means the server
answered with any status, so a 404 is `Ok`; when it is false, `Error` says why. `IsSuccess()` is
`Ok` with a 2xx status.

`AddAuth(header, scheme, key)` sends `<scheme> <key>`, or the key alone for an empty scheme. It
adds nothing for an empty key, so an endpoint configured without one stays unauthenticated.

The runtime aborts in-flight requests when the plugin unloads, so a plugin needs no HTTP cleanup;
requests sent after that are dropped and their completions never run. Do not block on a request
from the game thread.
