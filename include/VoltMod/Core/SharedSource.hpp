#pragma once

#include <VoltMod/Core/Event.hpp>
#include <functional>
#include <string>

namespace VoltMod
{

/**
 * @brief One engine-side source shared by several events, started and stopped on demand.
 *
 * A service with a single event gives @ref Event an @ref EventLifecycle directly. A service whose
 * events all come from one source - four movement events behind one RunCommand hook, a panel's
 * Clicked plus one event per button behind one click subscription - needs that source running
 * while *any* of them is listening:
 *
 * @code
 * Movement::Movement(...)
 *     : _source("Movement", [this] { return StartHook(); }, [this] { StopHook(); }),
 *       Pre(_source.Lifecycle()), Post(_source.Lifecycle()),
 *       PreCmd(_source.Lifecycle()), FilterCmd(_source.Lifecycle())
 * @endcode
 *
 * It starts when the first of those events gains a handler and stops when the last one still
 * holding a handler loses it. Returning false from @p start refuses the subscription that asked
 * and leaves the source stopped, so the next subscriber retries.
 *
 * Declare it above the events it feeds: they hold its address, so it must outlive them. Not
 * copyable or movable, for the same reason.
 *
 * Destroying it while an event still has handlers is a bug in the owner - those Subscriptions
 * point at events which are going away, and for a vtable hook that means a live hook into an
 * unloading module. It logs, naming @p what.
 */
class SharedSource
{
public:
    /**
     * @param what Names this in the leak log; use the service's own name.
     * @param start Runs when the first event starts listening. False refuses that subscription,
     *              having said why.
     * @param stop Runs when the last listening event goes quiet.
     */
    SharedSource(std::string what, std::function<bool()> start, std::function<void()> stop);
    ~SharedSource();

    SharedSource(const SharedSource&) = delete;
    SharedSource& operator=(const SharedSource&) = delete;

    /** A lifecycle for one event. Give one of these to every event fed from this source. */
    [[nodiscard]] EventLifecycle Lifecycle();

    /**
     * How many events fed from here are listening; the source is running while this is above zero.
     *
     * One event counts once however many handlers it holds: an @ref Event reports only its
     * empty-to-first and last-to-empty transitions, so a second handler on an event that already
     * had one is not a second subscriber here.
     *
     * Diagnostics and tests only - what a service can do is @ref Capabilities, not a flag here.
     */
    [[nodiscard]] int Subscribers() const noexcept { return _subscribers; }

private:
    bool AddSubscriber();
    void RemoveSubscriber();

    std::string _what;
    std::function<bool()> _start;
    std::function<void()> _stop;
    int _subscribers = 0;
};

}  // namespace VoltMod
