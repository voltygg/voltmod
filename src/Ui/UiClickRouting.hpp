#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Ui/UiClick.hpp>
#include <string>
#include <unordered_map>

namespace VoltMod::Internal
{

/** One event per Button id a panel has been asked about. `std::unordered_map` because a node is
 *  never moved: each event's @ref Event::Lifecycle holds the panel state's address, and every live
 *  Subscription holds the event's. */
using UiButtonEvents = std::unordered_map<std::string, Event<int>>;

/**
 * Fan one press out to a panel: nothing unless @p click came from @p layout, then @p clicked and,
 * if the id is one the panel has an event for, that event.
 *
 * The @ref UiPanel half of click routing with the entity system taken out, so the filtering and
 * the fan-out are unit-tested on their own. Returns whether the press was this panel's.
 */
bool RouteUiClick(const UiClick& click, EntityRef layout, Event<const UiClick&>& clicked, UiButtonEvents& buttons);

}  // namespace VoltMod::Internal
