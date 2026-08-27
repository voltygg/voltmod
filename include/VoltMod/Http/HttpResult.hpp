#pragma once

#include <functional>
#include <string>

namespace VoltMod
{

/** Outcome of an async HTTP request. `Ok` reflects transport success, not the HTTP status. */
struct HttpResult
{
    bool Ok = false;
    long StatusCode = 0;
    std::string Body;
    std::string Error;  // populated when Ok == false

    /**
     * Transport succeeded *and* the server answered 2xx. `Ok` on its own only says the request
     * reached a server, so a 404 or 500 still has `Ok == true`.
     */
    bool IsSuccess() const { return Ok && StatusCode >= 200 && StatusCode < 300; }
};

using HttpCompletion = std::function<void(const HttpResult&)>;

}  // namespace VoltMod
