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

/** A load step that returned an error. */
struct FailedStep
{
    std::string Name;
    std::string Reason;
    bool Required = false;  ///< The load aborts.
};

/**
 * @brief The named steps of one plugin load, remembering only the ones that fail.
 *
 * `Runtime::Start` and plugin `OnLoad` run their work through @ref Optional and @ref Required.
 * MetamodPlugin logs @ref Summary and copies @ref AbortReason into Metamod's error buffer.
 */
class LoadSteps
{
public:
    /** Run @p step. When it fails the load continues without that feature. Returns whether it succeeded. */
    bool Optional(std::string_view name, const std::function<Status()>& step);

    /** Run @p step. When it fails, return false from OnLoad. Returns whether it succeeded. */
    bool Required(std::string_view name, const std::function<Status()>& step);

    /** `N load steps in X ms`, then one line per failed step. */
    std::string Summary() const;

    /** `<name>: <reason>` of the first failed required step, or empty. Short enough for Metamod's error buffer. */
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
