#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Runtime.hpp>
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

    auto send = [slot, render = std::move(render)]() {
        VoltMod::Detail::Rt().Messages.SendCenterHtml(slot, render(slot));
    };
    send();
    _timers[slot] = VoltMod::Detail::Rt().Scheduler.Repeat(refreshMs, send);
}

void PersistentCenterHtml::Stop(int slot)
{
    if (!ValidSlot(slot) || !_timers[slot])
        return;
    _timers[slot].Reset();
    VoltMod::Detail::Rt().Messages.ClearCenterHtml(slot);
}

void PersistentCenterHtml::StopAll()
{
    for (int slot = 0; slot < MaxSlots; ++slot)
        Stop(slot);
}

}  // namespace VoltMod::Sdk
