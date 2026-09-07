#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Http/HttpClient.hpp>
#include <atomic>
#include <chrono>
#include <cpr/cpr.h>
#include <deque>
#include <functional>
#include <future>
#include <utility>

namespace VoltMod
{

static cpr::Header ParseHeaderLines(const std::vector<std::string>& headers)
{
    cpr::Header header;
    for (const auto& line : headers)
    {
        const size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        size_t valueStart = colon + 1;
        while (valueStart < line.size() && line[valueStart] == ' ')
            ++valueStart;
        header[line.substr(0, colon)] = line.substr(valueStart);
    }
    return header;
}

static HttpResult ToHttpResult(cpr::Response&& response)
{
    HttpResult result;
    if (response.error)
    {
        result.Error = response.error.message;
    }
    else
    {
        result.Ok = true;
        result.StatusCode = static_cast<long>(response.status_code);
        result.Body = std::move(response.text);
    }
    return result;
}

struct HttpClient::Impl
{
    struct Pending
    {
        std::future<HttpResult> Result;
        HttpCompletion OnComplete;
    };

    struct Queued
    {
        std::function<HttpResult()> Task;
        HttpCompletion OnComplete;
    };

    /** Maximum number of worker requests. Additional requests wait in the queue. */
    static constexpr size_t MaxInFlight = 4;

    std::vector<Pending> Items;
    std::deque<Queued> Waiting;

    /** Set by Stop to abort transfers from their progress callbacks, and never cleared: once
     *  stopped the client stays stopped, because a load cycle that is tearing down must not
     *  start another thread that could return into an unmapped DLL. Shared with the workers,
     *  which may outlive this Impl by the moment it takes them to notice. */
    std::shared_ptr<std::atomic_bool> Cancelled = std::make_shared<std::atomic_bool>(false);

    /** Queued requests are started only by StartWaiting, which enforces the in-flight cap. */
    void Launch(Queued&& queued)
    {
        Waiting.push_back(std::move(queued));
        StartWaiting();
    }

    void StartWaiting()
    {
        while (!Waiting.empty() && Items.size() < MaxInFlight)
        {
            Queued next = std::move(Waiting.front());
            Waiting.pop_front();
            Items.push_back({std::async(std::launch::async, std::move(next.Task)), std::move(next.OnComplete)});
        }
    }
};

HttpClient::HttpClient(Scheduler& scheduler)
    : _impl(std::make_unique<Impl>()), _onFrame(scheduler.EveryFrame([this] { DispatchCompletions(); }))
{}

// Destruction joins workers so they cannot call into an unmapped plugin. Stop() is idempotent.
HttpClient::~HttpClient()
{
    Stop();
}

void HttpClient::Stop()
{
    // Ask in-flight transfers to abort before waiting on them. std::async's future blocks on
    // destruction regardless, so without this a single stalled endpoint holds unload for the
    // whole request timeout.
    _impl->Cancelled->store(true, std::memory_order_relaxed);

    // Join workers during plugin unload, then discard completions they did not deliver. This keeps
    // meta reload from leaving threads pointing into the unmapped DLL.
    for (auto& p : _impl->Items)
        p.Result.wait();
    _impl->Items.clear();
    _impl->Waiting.clear();  // never started, so nothing to wait on
}

void HttpClient::Send(HttpRequest request, HttpCompletion onComplete)
{
    if (_impl->Cancelled->load(std::memory_order_relaxed))
    {
        // Dropped rather than queued: the completion would have nowhere safe to run.
        Log::Warn("http: dropping request to '{}' - the client is stopped.", request.Url);
        return;
    }

    // The worker touches only CPR + strings, never engine state; the callback is replayed on the
    // game thread in DispatchCompletions().
    auto task = [request = std::move(request), cancelled = _impl->Cancelled]() -> HttpResult {
        const cpr::Url url{request.Url};
        const cpr::Header headers = ParseHeaderLines(request.Headers);
        const cpr::Timeout timeout{std::chrono::milliseconds{request.TimeoutMs}};

        // Returning false from the progress callback aborts the transfer, which is what lets
        // Stop() interrupt a request that would otherwise run to its timeout.
        const cpr::ProgressCallback progress{
            [cancelled](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) {
                return !cancelled->load(std::memory_order_relaxed);
            }};

        switch (request.Method)
        {
        case HttpMethod::Get:
            return ToHttpResult(cpr::Get(url, headers, timeout, progress));
        case HttpMethod::Post:
            return ToHttpResult(cpr::Post(url, cpr::Body{request.Body}, headers, timeout, progress));
        case HttpMethod::Put:
            return ToHttpResult(cpr::Put(url, cpr::Body{request.Body}, headers, timeout, progress));
        case HttpMethod::Patch:
            return ToHttpResult(cpr::Patch(url, cpr::Body{request.Body}, headers, timeout, progress));
        case HttpMethod::Delete:
            return ToHttpResult(cpr::Delete(url, cpr::Body{request.Body}, headers, timeout, progress));
        }

        return {.Error = "unsupported HTTP method"};
    };

    _impl->Launch({std::move(task), std::move(onComplete)});
}

void HttpClient::Get(std::string url, HttpCompletion onComplete, std::vector<std::string> headers, long timeoutMs)
{
    Send({.Method = HttpMethod::Get, .Url = std::move(url), .Headers = std::move(headers), .TimeoutMs = timeoutMs},
         std::move(onComplete));
}

void HttpClient::Post(std::string url, std::string body, HttpCompletion onComplete, std::vector<std::string> headers,
                      long timeoutMs)
{
    Send({.Method = HttpMethod::Post,
          .Url = std::move(url),
          .Body = std::move(body),
          .Headers = std::move(headers),
          .TimeoutMs = timeoutMs},
         std::move(onComplete));
}

void HttpClient::Put(std::string url, std::string body, HttpCompletion onComplete, std::vector<std::string> headers,
                     long timeoutMs)
{
    Send({.Method = HttpMethod::Put,
          .Url = std::move(url),
          .Body = std::move(body),
          .Headers = std::move(headers),
          .TimeoutMs = timeoutMs},
         std::move(onComplete));
}

void HttpClient::Patch(std::string url, std::string body, HttpCompletion onComplete, std::vector<std::string> headers,
                       long timeoutMs)
{
    Send({.Method = HttpMethod::Patch,
          .Url = std::move(url),
          .Body = std::move(body),
          .Headers = std::move(headers),
          .TimeoutMs = timeoutMs},
         std::move(onComplete));
}

void HttpClient::Delete(std::string url, HttpCompletion onComplete, std::vector<std::string> headers, long timeoutMs)
{
    Send({.Method = HttpMethod::Delete, .Url = std::move(url), .Headers = std::move(headers), .TimeoutMs = timeoutMs},
         std::move(onComplete));
}

void HttpClient::DispatchCompletions()
{
    auto& items = _impl->Items;

    // Collect ready completions and compact the list *before* invoking callbacks: a callback may
    // re-enter Post() and append to items.
    std::vector<Impl::Pending> ready;
    for (auto it = items.begin(); it != items.end();)
    {
        if (it->Result.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            ready.push_back(std::move(*it));
            it = items.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Start whatever was waiting on a slot before running callbacks, so a queued request is not
    // held back by however long the completions take.
    _impl->StartWaiting();

    for (auto& p : ready)
    {
        if (p.OnComplete)
            p.OnComplete(p.Result.get());
    }
}

}  // namespace VoltMod
