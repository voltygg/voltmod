#pragma once

#include <functional>
#include <string>

namespace VoltMod
{

/** How an HTTP request ended. */
struct HttpResult
{
    bool Ok = false;  ///< the server answered, with any status; when false, Error says why
    long StatusCode = 0;
    std::string Body;
    std::string Error;

    /** The server answered with a 2xx status. */
    bool IsSuccess() const { return Ok && StatusCode >= 200 && StatusCode < 300; }
};

using HttpCompletion = std::function<void(const HttpResult&)>;

}  // namespace VoltMod
