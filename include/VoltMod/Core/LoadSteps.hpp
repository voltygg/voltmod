#pragma once

#include <VoltMod/Core/Result.hpp>
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** A load step that failed. */
struct FailedStep
{
    std::string Name;
    std::string Reason;
    bool Required = false;  ///< A required failure aborts the load.
};

/**
 * @brief Named steps for one plugin load, retaining failed steps.
 *
 * `Runtime::Start` and plugin `OnLoad` run work through @ref Optional and @ref Required.
 * MetamodPlugin logs @ref Summary and copies @ref AbortReason to Metamod's error buffer.
 */
class LoadSteps
{
public:
    /** Run @p step. A failure disables that feature and returns false. */
    bool Optional(std::string_view name, const std::function<Status()>& step);

    /** Run @p step. A failure returns false from OnLoad. */
    bool Required(std::string_view name, const std::function<Status()>& step);

    /** Return `N load steps in X ms`, followed by one line per failure. */
    std::string Summary() const;

    /** Return `<name>: <reason>` for the first required failure, or empty if none failed. */
    std::string AbortReason() const;

    const std::vector<FailedStep>& Failures() const { return _failures; }
    size_t Count() const { return _count; }

private:
    bool Record(std::string_view name, Status result, bool required);

    std::vector<FailedStep> _failures;
    size_t _count = 0;
    std::chrono::steady_clock::time_point _started = std::chrono::steady_clock::now();
};

}  // namespace VoltMod
