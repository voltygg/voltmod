#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/SharedSource.hpp>
#include <utility>

namespace VoltMod
{

SharedSource::SharedSource(std::string what, std::function<bool()> start, std::function<void()> stop)
    : _what(std::move(what)), _start(std::move(start)), _stop(std::move(stop))
{}

SharedSource::~SharedSource()
{
    // Never leave a handler pointing into state that is going away.
    if (_subscribers != 0)
        Log::Error("{}: {} event(s) still had handlers when the source went away; one may dangle.", _what,
                   _subscribers);
}

EventLifecycle SharedSource::Lifecycle()
{
    return {.OnFirst = [this] { return AddSubscriber(); }, .OnLast = [this] { RemoveSubscriber(); }};
}

bool SharedSource::AddSubscriber()
{
    // A refused start leaves the count at zero, so the next subscriber retries.
    if (_subscribers == 0 && _start && !_start())
        return false;

    ++_subscribers;
    return true;
}

void SharedSource::RemoveSubscriber()
{
    if (_subscribers > 0 && --_subscribers == 0 && _stop)
        _stop();
}

}  // namespace VoltMod
