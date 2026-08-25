#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Sdk/Messaging/PersistentCenterHtml.hpp>
#include <VoltMod/Sdk/Messaging/UserMessage.hpp>
#include <utility>

namespace VoltMod::Sdk
{

namespace
{
bool ValidSlot(int slot)
{
    return slot >= 0 && slot < PersistentCenterHtml::MaxSlots;
}
}  // namespace

void PersistentCenterHtml::Show(int slot, int refreshMs, std::function<std::string(int slot)> render)
{
    if (!ValidSlot(slot) || !render || refreshMs <= 0)
        return;

    Stop(slot);

    // The timer lives in _timers, so it is cancelled before `this` (and therefore _messages) goes
    // away - capturing the service by reference here is safe.
    auto send = [this, slot, render = std::move(render)]() { _messages.SendCenterHtml(slot, render(slot)); };
    send();
    _timers[slot] = _scheduler.Repeat(refreshMs, send);
}

void PersistentCenterHtml::Stop(int slot)
{
    if (!ValidSlot(slot) || !_timers[slot])
        return;
    _timers[slot].Reset();
    _messages.ClearCenterHtml(slot);
}

void PersistentCenterHtml::StopAll()
{
    for (int slot = 0; slot < MaxSlots; ++slot)
        Stop(slot);
}

}  // namespace VoltMod::Sdk
