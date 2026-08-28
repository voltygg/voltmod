#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
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

    static StageResult Ok(std::string_view detail = {}) { return {StageStatus::Ok, std::string(detail)}; }
    static StageResult Degraded(std::string_view detail) { return {StageStatus::Degraded, std::string(detail)}; }
    static StageResult Skipped(std::string detail) { return {StageStatus::Skipped, std::move(detail)}; }
    static StageResult Failed(std::string_view detail) { return {StageStatus::Failed, std::string(detail)}; }
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
 * `Runtime::Start` and plugin `OnLoad` record their steps here. Dependent stages
 * can use IsOk() and return Skipped instead of producing a secondary failure.
 * MetamodPlugin copies FirstFailure() to Metamod's error buffer and logs
 * Summary().
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

}  // namespace VoltMod
