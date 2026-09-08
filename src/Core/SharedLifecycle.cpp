#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/SharedLifecycle.hpp>
#include <utility>

namespace VoltMod
{

SharedLifecycle::SharedLifecycle(std::string what, std::function<bool()> start, std::function<void()> stop)
    : _what(std::move(what)), _start(std::move(start)), _stop(std::move(stop))
{}

SharedLifecycle::~SharedLifecycle()
{
    // Never leave a handler pointing into state that is going away.
    if (_listening != 0)
        Log::Error("{}: {} event(s) still had handlers when the source went away; one may dangle.", _what,
                   _listening);
}

EventLifecycle SharedLifecycle::ForEvent()
{
    return {.OnFirst = [this] { return AddListener(); }, .OnLast = [this] { RemoveListener(); }};
}

bool SharedLifecycle::AddListener()
{
    // A refused start leaves the count at zero, so the next subscriber retries.
    if (_listening == 0 && _start && !_start())
        return false;

    ++_listening;
    return true;
}

void SharedLifecycle::RemoveListener()
{
    if (_listening > 0 && --_listening == 0 && _stop)
        _stop();
}

}  // namespace VoltMod
