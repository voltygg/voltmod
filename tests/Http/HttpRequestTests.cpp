#include <VoltMod/Http/HttpClient.hpp>
#include <doctest/doctest.h>

using VoltMod::HttpRequest;
using VoltMod::HttpResult;

TEST_CASE("AddHeader writes the Key: Value line the client parses back")
{
    HttpRequest request;
    request.AddHeader("Content-Type", "application/json");

    REQUIRE(request.Headers.size() == 1);
    CHECK(request.Headers[0] == "Content-Type: application/json");
}

TEST_CASE("AddAuth prefixes the scheme")
{
    HttpRequest request;
    request.AddAuth("Authorization", "Bearer", "abc123");

    REQUIRE(request.Headers.size() == 1);
    CHECK(request.Headers[0] == "Authorization: Bearer abc123");
}

TEST_CASE("An empty scheme sends the key verbatim")
{
    HttpRequest request;
    request.AddAuth("X-Api-Key", "", "abc123");

    REQUIRE(request.Headers.size() == 1);
    CHECK(request.Headers[0] == "X-Api-Key: abc123");
}

TEST_CASE("An empty key adds no header at all")
{
    HttpRequest request;
    request.AddAuth("Authorization", "Bearer", "");

    CHECK(request.Headers.empty());
}

TEST_CASE("IsSuccess needs both transport success and a 2xx status")
{
    CHECK(HttpResult{.Ok = true, .StatusCode = 200}.IsSuccess());
    CHECK(HttpResult{.Ok = true, .StatusCode = 204}.IsSuccess());
    CHECK(HttpResult{.Ok = true, .StatusCode = 299}.IsSuccess());

    CHECK_FALSE(HttpResult{.Ok = true, .StatusCode = 404}.IsSuccess());
    CHECK_FALSE(HttpResult{.Ok = true, .StatusCode = 500}.IsSuccess());
    CHECK_FALSE(HttpResult{.Ok = true, .StatusCode = 302}.IsSuccess());
    CHECK_FALSE(HttpResult{.Ok = true, .StatusCode = 199}.IsSuccess());

    // A transport failure carries no status at all, so Ok is what rules it out.
    CHECK_FALSE(HttpResult{.Ok = false, .StatusCode = 200}.IsSuccess());
}
