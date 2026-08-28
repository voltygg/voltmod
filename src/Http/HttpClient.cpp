#include <VoltMod/Http/HttpClient.hpp>
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

    /** Each in-flight request is its own std::async thread. Unbounded, that let a burst of
     *  requests spawn a thread apiece on a box already budgeting its cores for the tick loop;
     *  past this many, requests wait their turn. */
    static constexpr size_t MaxInFlight = 4;

    std::vector<Pending> Items;
    std::deque<Queued> Waiting;

    /** Everything queues; StartWaiting is the one place a request is actually started, so the
     *  in-flight cap is tested once. */
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

// Self-cleaning: a client destroyed without an explicit Stop() still joins its workers rather than
// leaving them pointing into a DLL that is about to be unmapped. Stop() is idempotent.
HttpClient::~HttpClient()
{
    Stop();
}

void HttpClient::Stop()
{
    // Block until in-flight requests finish, then drop their (unrun) completions. Joining here - on
    // the plugin Unload path, not in DllMain - is what keeps `meta reload` from leaving live worker
    // threads pointing into the unmapped DLL.
    for (auto& p : _impl->Items)
        p.Result.wait();
    _impl->Items.clear();
    _impl->Waiting.clear();  // never started, so nothing to wait on
}

void HttpClient::Send(HttpRequest request, HttpCompletion onComplete)
{
    // The worker touches only CPR + strings, never engine state; the callback is replayed on the
    // game thread in DispatchCompletions().
    auto task = [request = std::move(request)]() -> HttpResult {
        const cpr::Url url{request.Url};
        const cpr::Header headers = ParseHeaderLines(request.Headers);
        const cpr::Timeout timeout{std::chrono::milliseconds{request.TimeoutMs}};

        switch (request.Method)
        {
        case HttpMethod::Get:
            return ToHttpResult(cpr::Get(url, headers, timeout));
        case HttpMethod::Post:
            return ToHttpResult(cpr::Post(url, cpr::Body{request.Body}, headers, timeout));
        case HttpMethod::Put:
            return ToHttpResult(cpr::Put(url, cpr::Body{request.Body}, headers, timeout));
        case HttpMethod::Patch:
            return ToHttpResult(cpr::Patch(url, cpr::Body{request.Body}, headers, timeout));
        case HttpMethod::Delete:
            return ToHttpResult(cpr::Delete(url, cpr::Body{request.Body}, headers, timeout));
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
