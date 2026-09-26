#include <VoltMod/Http/HttpClient.hpp>
#include <doctest/doctest.h>

using VoltMod::HttpRequest;
using VoltMod::HttpResult;

TEST_CASE("AddAuth prefixes the scheme, and adds nothing without a key")
{
    HttpRequest bearer;
    bearer.AddAuth("Authorization", "Bearer", "abc123");
    CHECK(bearer.Headers.at("Authorization") == "Bearer abc123");

    // An empty scheme is how an API-key header is sent.
    HttpRequest apiKey;
    apiKey.AddAuth("X-Api-Key", "", "abc123");
    CHECK(apiKey.Headers.at("X-Api-Key") == "abc123");

    HttpRequest none;
    none.AddAuth("Authorization", "Bearer", "");
    CHECK(none.Headers.empty());
}

TEST_CASE("IsSuccess needs a response with a 2xx status")
{
    CHECK(HttpResult{.Ok = true, .StatusCode = 200}.IsSuccess());
    CHECK(HttpResult{.Ok = true, .StatusCode = 204}.IsSuccess());
    CHECK(HttpResult{.Ok = true, .StatusCode = 299}.IsSuccess());

    CHECK_FALSE(HttpResult{.Ok = true, .StatusCode = 404}.IsSuccess());
    CHECK_FALSE(HttpResult{.Ok = true, .StatusCode = 500}.IsSuccess());
    CHECK_FALSE(HttpResult{.Ok = true, .StatusCode = 302}.IsSuccess());
    CHECK_FALSE(HttpResult{.Ok = true, .StatusCode = 199}.IsSuccess());

    CHECK_FALSE(HttpResult{.Ok = false, .StatusCode = 200}.IsSuccess());
}
