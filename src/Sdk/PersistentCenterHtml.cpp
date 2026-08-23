#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/PersistentCenterHtml.hpp>
#include <CS2Kit/Sdk/UserMessage.hpp>
#include <utility>

namespace CS2Kit::Sdk
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

    auto send = [slot, render = std::move(render)]() {
        CS2Kit::Detail::Rt().Messages.SendCenterHtml(slot, render(slot));
    };
    send();
    _timers[slot] = CS2Kit::Detail::Rt().Scheduler.Repeat(refreshMs, send);
}

void PersistentCenterHtml::Stop(int slot)
{
    if (!ValidSlot(slot) || !_timers[slot])
        return;
    _timers[slot].Reset();
    CS2Kit::Detail::Rt().Messages.ClearCenterHtml(slot);
}

void PersistentCenterHtml::StopAll()
{
    for (int slot = 0; slot < MaxSlots; ++slot)
        Stop(slot);
}

}  // namespace CS2Kit::Sdk
