#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Http/HttpClient.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cpr/cpr.h>
#include <deque>
#include <functional>
#include <future>
#include <iterator>
#include <utility>
#include <vector>

namespace VoltMod
{

static constexpr size_t MaxRunning = 4;

struct RunningRequest
{
    std::future<HttpResult> Result;
    HttpCompletion OnComplete;
};

struct WaitingRequest
{
    std::function<HttpResult()> Run;
    HttpCompletion OnComplete;
};

struct HttpClient::Requests
{
    std::vector<RunningRequest> Running;
    std::deque<WaitingRequest> Waiting;
    std::atomic_bool Stopped = false;  // never cleared: a stopped client belongs to an unloading plugin

    void StartWaiting()
    {
        while (!Waiting.empty() && Running.size() < MaxRunning)
        {
            WaitingRequest next = std::move(Waiting.front());
            Waiting.pop_front();
            Running.push_back({std::async(std::launch::async, std::move(next.Run)), std::move(next.OnComplete)});
        }
    }
};

static HttpResult ToResult(cpr::Response&& response)
{
    if (response.error)
    {
        return {.Error = std::move(response.error.message)};
    }
    return {.Ok = true, .StatusCode = static_cast<long>(response.status_code), .Body = std::move(response.text)};
}

/** Runs on a worker thread, so it touches nothing but the request. */
static HttpResult Perform(const HttpRequest& request, const std::atomic_bool& stopped)
{
    const cpr::Url url{request.Url};
    const cpr::Header headers(request.Headers.begin(), request.Headers.end());
    const cpr::Timeout timeout{std::chrono::milliseconds{request.TimeoutMs}};
    // Returning false aborts the transfer, so Stop does not wait out a stalled endpoint's timeout.
    const cpr::ProgressCallback progress{[&stopped](auto&&...) { return !stopped; }};

    switch (request.Method)
    {
    case HttpMethod::Get:
        return ToResult(cpr::Get(url, headers, timeout, progress));
    case HttpMethod::Post:
        return ToResult(cpr::Post(url, cpr::Body{request.Body}, headers, timeout, progress));
    case HttpMethod::Put:
        return ToResult(cpr::Put(url, cpr::Body{request.Body}, headers, timeout, progress));
    case HttpMethod::Patch:
        return ToResult(cpr::Patch(url, cpr::Body{request.Body}, headers, timeout, progress));
    case HttpMethod::Delete:
        return ToResult(cpr::Delete(url, cpr::Body{request.Body}, headers, timeout, progress));
    }
    return {.Error = "unsupported HTTP method"};
}

HttpClient::HttpClient(Scheduler& scheduler)
    : _requests(std::make_unique<Requests>()), _onFrame(scheduler.EveryFrame([this] { RunCompletions(); }))
{}

HttpClient::~HttpClient()
{
    Stop();
}

void HttpClient::Send(HttpRequest request, HttpCompletion onComplete)
{
    if (_requests->Stopped)
    {
        Log::Warn("http: dropped a request to '{}' because the client is stopped.", request.Url);
        return;
    }

    // Stop joins every worker before _requests goes away, so the flag outlives them.
    auto run = [request = std::move(request), &stopped = _requests->Stopped] { return Perform(request, stopped); };
    _requests->Waiting.push_back({std::move(run), std::move(onComplete)});
    _requests->StartWaiting();
}

void HttpClient::Stop()
{
    _requests->Stopped = true;
    for (RunningRequest& request : _requests->Running)
    {
        request.Result.wait();
    }
    _requests->Running.clear();
    _requests->Waiting.clear();
}

void HttpClient::RunCompletions()
{
    // Take the finished requests out first: a completion may Send and grow the list.
    auto& running = _requests->Running;
    const auto finished = std::ranges::stable_partition(running, [](const RunningRequest& request) {
        return request.Result.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    });
    std::vector<RunningRequest> done(std::make_move_iterator(finished.begin()),
                                     std::make_move_iterator(finished.end()));
    running.erase(finished.begin(), finished.end());

    // Before the completions, so a waiting request does not wait on them too.
    _requests->StartWaiting();

    for (RunningRequest& request : done)
    {
        if (request.OnComplete)
        {
            request.OnComplete(request.Result.get());
        }
    }
}

}  // namespace VoltMod
