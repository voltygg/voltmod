#pragma once

#include "HttpResult.hpp"

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

/** A complete HTTP request. Headers are full "Key: Value" lines. */
struct HttpRequest
{
    HttpMethod Method = HttpMethod::Get;
    std::string Url;
    std::string Body;
    std::vector<std::string> Headers;
    long TimeoutMs = 8000;

    /** Append one header as the "Key: Value" line the client expects. */
    void AddHeader(std::string_view name, std::string_view value)
    {
        Headers.push_back(std::string(name) + ": " + std::string(value));
    }

    /**
     * Append a credential header, e.g. `AddAuth("Authorization", "Bearer", key)`. An empty
     * @p scheme sends @p key verbatim. An empty @p key is a no-op, so an endpoint configured
     * without one stays unauthenticated rather than sending an empty credential.
     */
    void AddAuth(std::string_view header, std::string_view scheme, std::string_view key)
    {
        if (key.empty())
            return;
        AddHeader(header, scheme.empty() ? std::string(key) : std::string(scheme) + " " + std::string(key));
    }
};

/**
 * Async HTTP client. Requests run off the game thread (CPR's worker pool); completions are queued
 * and replayed on the game thread via `DispatchCompletions()` so callbacks may safely touch engine
 * state. No engine API may be called from a completion before that dispatch.
 *
 * The dispatch runs from a per-frame scheduler subscription the client registers for itself, so
 * nothing outside has a per-frame list to keep in sync.
 */
class HttpClient
{
public:
    /** @p scheduler must outlive the client; the per-frame subscription unregisters in the
     *  destructor. */
    explicit HttpClient(Scheduler& scheduler);
    /** Runs @ref Stop, so a client that is merely destroyed still joins its workers. */
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /** Wait out any in-flight requests and drop their (unrun) completions. Idempotent. */
    void Stop();

    /** Enqueue a request. `onComplete` runs on the game thread on a later dispatch. */
    void Send(HttpRequest request, HttpCompletion onComplete);

    /** Convenience helpers over Send. Use Send directly for less common request shapes. */
    void Get(std::string url, HttpCompletion onComplete, std::vector<std::string> headers = {}, long timeoutMs = 8000);
    void Post(std::string url, std::string body, HttpCompletion onComplete, std::vector<std::string> headers = {},
              long timeoutMs = 8000);
    void Put(std::string url, std::string body, HttpCompletion onComplete, std::vector<std::string> headers = {},
             long timeoutMs = 8000);
    void Patch(std::string url, std::string body, HttpCompletion onComplete, std::vector<std::string> headers = {},
               long timeoutMs = 8000);
    void Delete(std::string url, HttpCompletion onComplete, std::vector<std::string> headers = {},
                long timeoutMs = 8000);

private:
    /** Invoke all ready completions on the calling (game) thread. */
    void DispatchCompletions();

    struct Impl;
    std::unique_ptr<Impl> _impl;
    /** Declared after _impl so per-frame delivery stops before the queue its callback reads
     *  goes away. */
    Subscription _onFrame;
};

}  // namespace VoltMod
