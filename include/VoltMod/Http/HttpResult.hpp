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
};

using HttpCompletion = std::function<void(const HttpResult&)>;

}  // namespace VoltMod
