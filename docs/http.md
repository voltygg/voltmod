# HTTP {#http_guide}

[TOC]

`CS2Kit::Http` gives you async requests whose completions run on the game thread:

- **HttpClient** - GET/POST on CPR's worker pool; completions are queued and replayed on the game thread from a self-registered per-frame pump
- **RestJsonApi** - helpers for the config-driven "call an operator-configured JSON endpoint and pull a field out of the response" shape

`HttpClient` is a kit service - reach it via `runtime.Http`. The `Runtime` destructor drains in-flight requests, so there is nothing to wire up or tear down.

## Requests

```cpp
#include <CS2Kit/Api.hpp>

runtime.Http.Post(url, body, {"Content-Type: application/json"}, /*timeoutMs=*/8000,
                   [](const CS2Kit::HttpResult& result) {
                       // Game thread - safe to touch players, menus, managers.
                       if (!result.Ok)
                           Log::Warn("request failed: {}", result.Error);
                   });

runtime.Http.Get(url, {}, 5000, [](const CS2Kit::HttpResult& result) { /* ... */ });
```

`HttpResult::Ok` reflects transport success only; check `StatusCode` for the HTTP verdict (or use `IsSuccess` from RestJsonApi, which means `Ok && 2xx`).

## Config-driven JSON endpoints (RestJsonApi)

`JsonPostSpec` describes an endpoint entirely from settings - URL, optional auth header, a JSON body template with `{token}` placeholders - so server operators can point a plugin at their own backend without code changes:

```cpp
#include <CS2Kit/Http/RestJsonApi.hpp>
using namespace CS2Kit::Http;

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

Requests run off-thread; **callbacks never run concurrently with game code**. Do not block on a request from the game thread - there is no synchronous API by design.
