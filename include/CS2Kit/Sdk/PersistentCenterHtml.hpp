#pragma once

#include <CS2Kit/Core/Subscription.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace CS2Kit::Sdk
{

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

    /** Start (or restart) re-sending `render(slot)`'s HTML to @p slot every @p refreshMs. */
    void Show(int slot, int refreshMs, std::function<std::string(int slot)> render);

    /** Stop re-sending and clear the panel. Safe when nothing is shown. */
    void Stop(int slot);

    void StopAll();

private:
    std::array<Core::Subscription, MaxSlots> _timers;
};

}  // namespace CS2Kit::Sdk
