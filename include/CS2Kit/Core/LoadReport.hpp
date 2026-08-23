#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace CS2Kit::Core
{

enum class StageStatus
{
    Ok,        ///< Fully initialized.
    Degraded,  ///< Loaded with reduced functionality.
    Skipped,   ///< Not attempted (usually because a dependency is not Ok).
    Failed     ///< Load-aborting failure.
};

/** @brief Outcome of one load stage. Return from the body passed to LoadReport::Run. */
struct StageResult
{
    StageStatus Status = StageStatus::Ok;
    std::string Detail;

    static StageResult Ok(std::string detail = {}) { return {StageStatus::Ok, std::move(detail)}; }
    static StageResult Degraded(std::string detail) { return {StageStatus::Degraded, std::move(detail)}; }
    static StageResult Skipped(std::string detail) { return {StageStatus::Skipped, std::move(detail)}; }
    static StageResult Failed(std::string detail) { return {StageStatus::Failed, std::move(detail)}; }
};

struct StageRecord
{
    std::string Name;
    StageStatus Status = StageStatus::Ok;
    std::string Detail;
    double DurationMs = 0.0;
};

/**
 * @brief Named, timed load stages with a per-stage report.
 *
 * Both CS2Kit::Initialize and a plugin's OnLoad record their steps here
 * (`Engine().Core.LoadReport`). Later stages guard on earlier ones via IsOk()
 * and return StageResult::Skipped instead of failing with a secondary error;
 * MetamodPluginBase surfaces FirstFailure() in Metamod's error buffer and
 * logs Summary() after load.
 */
class LoadReport
{
public:
    /** @brief Time `body`, record its result under `name`, and return the status. */
    StageStatus Run(std::string_view name, const std::function<StageResult()>& body);

    /** @brief True when stage `name` was recorded with StageStatus::Ok (Degraded is not Ok). */
    bool IsOk(std::string_view name) const;

    /** @brief Aligned multi-line table of all stages with status, detail, and timing. */
    std::string Summary() const;

    /** @brief "<stage>: <detail>" of the first Failed stage, or empty. Kept short for Metamod's error buffer. */
    std::string FirstFailure() const;

    const std::vector<StageRecord>& Stages() const { return _stages; }

private:
    std::vector<StageRecord> _stages;
};

std::string_view ToString(StageStatus status);

}  // namespace CS2Kit::Core
