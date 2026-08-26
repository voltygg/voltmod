# HTTP {#http_guide}

[TOC]

`VoltMod/Http/` provides asynchronous requests with game-thread completions:

- `HttpClient` runs GET and POST on CPR's worker pool. Completions are queued and replayed on the game thread from a self-registered per-frame pump.
- `RestJsonApi` covers the config-driven shape: call an operator-configured JSON endpoint and pull a field out of the response.

`HttpClient` is a framework service; reach it through `runtime.Http`. The
`Runtime` destructor drains in-flight requests, so the plugin has no separate
shutdown step.

## Requests

```cpp
#include <VoltMod/Api.hpp>

runtime.Http.Post(url, body, {"Content-Type: application/json"}, /*timeoutMs=*/8000,
                   [](const VoltMod::HttpResult& result) {
                       // Game thread: safe to touch players, menus, managers.
                       if (!result.Ok)
                           Log::Warn("request failed: {}", result.Error);
                   });

runtime.Http.Get(url, {}, 5000, [](const VoltMod::HttpResult& result) { /* ... */ });
```

`HttpResult::Ok` reflects transport success only; check `StatusCode` for the HTTP verdict (or use `IsSuccess` from RestJsonApi, which means `Ok && 2xx`).

## Config-driven JSON endpoints

`JsonPostSpec` describes an endpoint entirely from settings (URL, optional auth header, and a JSON body template with `{token}` placeholders), so server operators can point a plugin at their own backend without code changes:

```cpp
#include <VoltMod/Http/RestJsonApi.hpp>
using VoltMod::JsonPostSpec;

JsonPostSpec spec{
    .Url = cfg.createRoomUrl,
    .ApiKey = cfg.apiKey,             // "" = no auth header
    .AuthHeader = "Authorization",
    .AuthScheme = "Bearer",           // "" sends the key verbatim
    .BodyTemplate = cfg.requestBody,  // nlohmann::json with {token} placeholders
    .TimeoutMs = 8000,
};

auto request = BuildJsonPost(spec, {{"steamId", std::to_string(steamId)}, {"playerName", name}});
if (request)
{
    Post(runtime.Http, std::move(*request), [](const HttpResult& result) {
        if (!IsSuccess(result))
            return;
        std::string url = ExtractField(result, "data.room.playerUrl");  // dot-path extraction
    });
}
```

## Threading

Requests run off-thread, but **callbacks never run concurrently with game code**.
Do not block on a request from the game thread; the API is asynchronous by
design.
