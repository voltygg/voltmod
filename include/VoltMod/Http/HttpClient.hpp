#pragma once

#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Http/HttpResult.hpp>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

enum class HttpMethod
{
    Get,
    Post,
    Put,
    Patch,
    Delete,
};

struct HttpRequest
{
    HttpMethod Method = HttpMethod::Get;
    std::string Url;
    std::string Body;
    std::map<std::string, std::string> Headers;
    long TimeoutMs = 8000;

    /**
     * Add a credential header, e.g. `AddAuth("Authorization", "Bearer", key)`; an empty @p scheme
     * sends @p key alone. Nothing is added without a key, so an unconfigured endpoint stays
     * unauthenticated instead of sending an empty credential.
     */
    void AddAuth(std::string_view header, std::string_view scheme, std::string_view key)
    {
        if (!key.empty())
        {
            Headers[std::string(header)] =
                scheme.empty() ? std::string(key) : std::string(scheme) + " " + std::string(key);
        }
    }
};

/**
 * @brief Runs HTTP requests off the game thread and their completions on it.
 *
 * Up to four requests run at once, each on its own thread; the rest wait their turn. Completions
 * run from a per-frame scheduler callback, so they may touch engine state.
 */
class HttpClient
{
public:
    /** @p scheduler must outlive the client. */
    explicit HttpClient(Scheduler& scheduler);
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /** Run @p request; @p onComplete runs on the game thread on a later frame. Dropped after @ref Stop. */
    void Send(HttpRequest request, HttpCompletion onComplete);

    /**
     * Abort and join the running requests and drop the waiting ones; no completion runs after this.
     * Later sends are dropped too, so an unloading plugin never starts a thread into its own module.
     */
    void Stop();

private:
    /** Each frame: run the completions of finished requests and start waiting ones. */
    void RunCompletions();

    struct Requests;
    std::unique_ptr<Requests> _requests;
    Subscription _onFrame;  // after _requests, so frames stop before the requests go away
};

}  // namespace VoltMod
