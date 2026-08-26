#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace VoltMod
{

/**
 * @brief Re-sends a center-HTML panel on a fixed interval until stopped. CS2 drops center-HTML
 * almost immediately (death, team switch, HUD updates), so a sticky panel must be re-sent
 * continuously - this owns that loop and nothing else. `render` runs every refresh, so live
 * content (countdowns) stays current. Deadline/expiry policy belongs to the owner's own timer.
 */
class CenterHtml
{
public:
    /** @p messages sends and clears the panel, @p scheduler drives the refresh. Both must outlive
     *  this object; pass `runtime.Messages` and `runtime.Scheduler`. */
    CenterHtml(Messages& messages, Scheduler& scheduler) : _messages(messages), _scheduler(scheduler) {}

    /** Start (or restart) re-sending `render(slot)`'s HTML to @p slot every @p refreshMs. */
    void Show(int slot, int refreshMs, std::function<std::string(int slot)> render);

    /** Stop re-sending and clear the panel. Safe when nothing is shown. */
    void Stop(int slot);

    void StopAll();

private:
    Messages& _messages;
    Scheduler& _scheduler;
    std::array<Subscription, MaxPlayers> _timers;
};

}  // namespace VoltMod
