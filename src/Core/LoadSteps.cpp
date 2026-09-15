#include <VoltMod/Core/LoadSteps.hpp>
#include <algorithm>
#include <chrono>
#include <format>

namespace VoltMod
{

bool LoadSteps::Optional(std::string_view name, const std::function<Status()>& step)
{
    return Record(name, step(), false);
}

bool LoadSteps::Required(std::string_view name, const std::function<Status()>& step)
{
    return Record(name, step(), true);
}

bool LoadSteps::Record(std::string_view name, Status result, bool required)
{
    ++_count;
    if (result)
        return true;

    _failures.push_back({.Name = std::string(name), .Reason = std::move(result.error().Detail), .Required = required});
    return false;
}

std::string LoadSteps::Summary() const
{
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _started);
    std::string out = std::format("{} load steps in {} ms", _count, elapsed.count());
    if (_failures.empty())
        return out + ", none failed";

    out += std::format(", {} failed:", _failures.size());
    for (const FailedStep& failed : _failures)
        out += std::format("\n  {} {}: {}", failed.Required ? "required" : "optional", failed.Name, failed.Reason);
    return out;
}

std::string LoadSteps::AbortReason() const
{
    const auto it = std::ranges::find(_failures, true, &FailedStep::Required);
    if (it == _failures.end())
        return {};
    return it->Reason.empty() ? it->Name : std::format("{}: {}", it->Name, it->Reason);
}

}  // namespace VoltMod
