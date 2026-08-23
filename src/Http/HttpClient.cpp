#include <CS2Kit/Http/HttpClient.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <cpr/cpr.h>
#include <future>
#include <utility>

namespace CS2Kit::Http
{

namespace
{

cpr::Header ParseHeaderLines(const std::vector<std::string>& headers)
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

HttpResult ToHttpResult(cpr::Response&& response)
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

}  // namespace

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

    void Launch(Queued&& queued)
    {
        if (Items.size() < MaxInFlight)
            Items.push_back({std::async(std::launch::async, std::move(queued.Task)), std::move(queued.OnComplete)});
        else
            Waiting.push_back(std::move(queued));
    }

    void PumpWaiting()
    {
        while (!Waiting.empty() && Items.size() < MaxInFlight)
        {
            Queued next = std::move(Waiting.front());
            Waiting.pop_front();
            Items.push_back({std::async(std::launch::async, std::move(next.Task)), std::move(next.OnComplete)});
        }
    }
};

HttpClient::HttpClient() : _impl(std::make_unique<Impl>()) {}

HttpClient::~HttpClient() = default;

void HttpClient::Start() {}

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

void HttpClient::Post(std::string url, std::string body, std::vector<std::string> headers, long timeoutMs,
                      HttpCompletion onComplete)
{
    // The worker touches only CPR + strings, never engine state; the callback is replayed on the
    // game thread in DispatchCompletions().
    auto task = [url = std::move(url), body = std::move(body), headers = std::move(headers),
                 timeoutMs]() -> HttpResult {
        return ToHttpResult(cpr::Post(cpr::Url{url}, cpr::Body{body}, ParseHeaderLines(headers),
                                      cpr::Timeout{std::chrono::milliseconds{timeoutMs}}));
    };

    _impl->Launch({std::move(task), std::move(onComplete)});
}

void HttpClient::Get(std::string url, std::vector<std::string> headers, long timeoutMs, HttpCompletion onComplete)
{
    auto task = [url = std::move(url), headers = std::move(headers), timeoutMs]() -> HttpResult {
        return ToHttpResult(
            cpr::Get(cpr::Url{url}, ParseHeaderLines(headers), cpr::Timeout{std::chrono::milliseconds{timeoutMs}}));
    };

    _impl->Launch({std::move(task), std::move(onComplete)});
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
    _impl->PumpWaiting();

    for (auto& p : ready)
    {
        if (p.OnComplete)
            p.OnComplete(p.Result.get());
    }
}

}  // namespace CS2Kit::Http
