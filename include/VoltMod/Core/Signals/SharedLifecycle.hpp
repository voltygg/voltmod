#pragma once

#include <VoltMod/Core/Signals/Event.hpp>
#include <functional>
#include <string>

namespace VoltMod
{

/**
 * @brief Keep one source active while any of several events has listeners.
 *
 * Each @ref ForEvent adapter counts its event once while that event has handlers. The first active
 * event calls @p start; the last one to become empty calls @p stop. If @p start returns false, the
 * subscription is rejected and a later subscriber retries.
 *
 * @code
 * Movement::Movement(...)
 *     : _lifecycle("Movement", [this] { return Install(); }, [this] { _hook.Reset(); }),
 *       Rewrite(_lifecycle.ForEvent()), Before(_lifecycle.ForEvent()), After(_lifecycle.ForEvent())
 * @endcode
 *
 * Adapters borrow this object, so declare it before the events they belong to. Destroying it with
 * active listeners is an ownership error that can leave a hook in an unloading module; the
 * destructor logs @p what.
 */
class SharedLifecycle
{
public:
    SharedLifecycle(std::string what, std::function<bool()> start, std::function<void()> stop);
    ~SharedLifecycle();

    SharedLifecycle(const SharedLifecycle&) = delete;
    SharedLifecycle& operator=(const SharedLifecycle&) = delete;

    [[nodiscard]] EventLifecycle ForEvent();

    /** Number of adapted events with at least one handler. For diagnostics and tests only. */
    [[nodiscard]] int ListeningEvents() const noexcept { return _listening; }

private:
    bool AddListener();
    void RemoveListener();

    std::string _what;
    std::function<bool()> _start;
    std::function<void()> _stop;
    int _listening = 0;
};

}  // namespace VoltMod
