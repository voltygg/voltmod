#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Messaging/CenterHtml.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <utility>

namespace VoltMod::Messaging
{

void CenterHtml::Show(int slot, int refreshMs, std::function<std::string(int slot)> render)
{
    if (!Core::IsValidSlot(slot) || !render || refreshMs <= 0)
        return;

    Stop(slot);

    // The timer lives in _timers, so it is cancelled before `this` (and therefore _messages) goes
    // away - capturing the service by reference here is safe.
    auto send = [this, slot, render = std::move(render)]() { _messages.SendCenterHtml(slot, render(slot)); };
    send();
    _timers[slot] = _scheduler.Repeat(refreshMs, send);
}

void CenterHtml::Stop(int slot)
{
    if (!Core::IsValidSlot(slot) || !_timers[slot])
        return;
    _timers[slot].Reset();
    _messages.ClearCenterHtml(slot);
}

void CenterHtml::StopAll()
{
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
        Stop(slot);
}

}  // namespace VoltMod::Messaging
