#pragma once

#include "HttpResult.hpp"

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <memory>
#include <string>
#include <vector>

namespace VoltMod::Http
{

/**
 * Async HTTP client. Requests run off the game thread (CPR's worker pool); completions are queued
 * and replayed on the game thread via `DispatchCompletions()` so callbacks may safely touch engine
 * state. No engine API may be called from a completion before that dispatch.
 *
 * The dispatch runs from a per-frame scheduler pump the client registers for itself, so nothing
 * outside has a pump list to keep in sync.
 */
class HttpClient
{
public:
    /** @p scheduler must outlive the client; the pump unregisters in the destructor. */
    explicit HttpClient(Core::Scheduler& scheduler);
    /** Runs @ref Stop, so a client that is merely destroyed still joins its workers. */
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /** Reserved for API symmetry; CPR initialises libcurl lazily, so this is a no-op. */
    void Start();

    /** Wait out any in-flight requests and drop their (unrun) completions. Idempotent. */
    void Stop();

    /**
     * Enqueue an async POST. `headers` are full "Key: Value" lines. `onComplete` runs on the game
     * thread on a later `DispatchCompletions()`.
     */
    void Post(std::string url, std::string body, std::vector<std::string> headers, long timeoutMs,
              HttpCompletion onComplete);

    /** Enqueue an async GET. Same threading contract as Post. */
    void Get(std::string url, std::vector<std::string> headers, long timeoutMs, HttpCompletion onComplete);

    /** Invoke all ready completions on the calling (game) thread. */
    void DispatchCompletions();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
    /** Declared after _impl so the pump stops before the queue its callback drains goes away. */
    Core::Subscription _pump;
};

}  // namespace VoltMod::Http
