#include <VoltMod/Core/LoadReport.hpp>
#include <algorithm>
#include <chrono>
#include <format>

namespace VoltMod::Core
{

std::string_view ToString(StageStatus status)
{
    switch (status)
    {
    case StageStatus::Ok:
        return "ok";
    case StageStatus::Degraded:
        return "degraded";
    case StageStatus::Skipped:
        return "skipped";
    case StageStatus::Failed:
        return "failed";
    }
    return "unknown";
}

StageStatus LoadReport::Run(std::string_view name, const std::function<StageResult()>& body)
{
    const auto start = std::chrono::steady_clock::now();
    StageResult result = body();
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);

    _stages.push_back({std::string(name), result.Status, std::move(result.Detail), elapsed.count()});
    return result.Status;
}

bool LoadReport::IsOk(std::string_view name) const
{
    auto it = std::ranges::find(_stages, name, &StageRecord::Name);
    return it != _stages.end() && it->Status == StageStatus::Ok;
}

std::string LoadReport::Summary() const
{
    size_t nameWidth = 0;
    for (const auto& stage : _stages)
        nameWidth = std::max(nameWidth, stage.Name.size());

    double totalMs = 0.0;
    std::string out = "Load report:";
    for (const auto& stage : _stages)
    {
        totalMs += stage.DurationMs;
        out += std::format("\n  {:<{}}  {:<8}  {:>7.1f} ms", stage.Name, nameWidth, ToString(stage.Status),
                           stage.DurationMs);
        if (!stage.Detail.empty())
            out += std::format("  {}", stage.Detail);
    }
    out += std::format("\n  {:<{}}  {:>19.1f} ms", "total", nameWidth + 10, totalMs);
    return out;
}

std::string LoadReport::FirstFailure() const
{
    auto it = std::ranges::find(_stages, StageStatus::Failed, &StageRecord::Status);
    if (it == _stages.end())
        return {};
    return it->Detail.empty() ? it->Name : std::format("{}: {}", it->Name, it->Detail);
}

}  // namespace VoltMod::Core
