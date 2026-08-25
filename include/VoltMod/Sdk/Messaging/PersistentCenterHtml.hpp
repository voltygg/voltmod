#pragma once

#include <VoltMod/Core/Subscription.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace VoltMod::Core
{
class Scheduler;
}

namespace VoltMod::Sdk
{

class MessageSystem;

/**
 * @brief Re-sends a center-HTML panel on a fixed interval until stopped. CS2 drops center-HTML
 * almost immediately (death, team switch, HUD updates), so a sticky panel must be re-sent
 * continuously - this owns that loop and nothing else. `render` runs every refresh, so live
 * content (countdowns) stays current. Deadline/expiry policy belongs to the owner's own timer.
 */
class PersistentCenterHtml
{
public:
    static constexpr int MaxSlots = 64;

    /** @p messages sends and clears the panel, @p scheduler drives the refresh. Both must outlive
     *  this object; pass `runtime.Messages` and `runtime.Scheduler`. */
    PersistentCenterHtml(MessageSystem& messages, Core::Scheduler& scheduler)
        : _messages(messages), _scheduler(scheduler)
    {}

    /** Start (or restart) re-sending `render(slot)`'s HTML to @p slot every @p refreshMs. */
    void Show(int slot, int refreshMs, std::function<std::string(int slot)> render);

    /** Stop re-sending and clear the panel. Safe when nothing is shown. */
    void Stop(int slot);

    void StopAll();

private:
    MessageSystem& _messages;
    Core::Scheduler& _scheduler;
    std::array<Core::Subscription, MaxSlots> _timers;
};

}  // namespace VoltMod::Sdk
