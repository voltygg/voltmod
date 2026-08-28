#include "Ui/UiClickRouting.hpp"

namespace VoltMod::Internal
{

bool RouteUiClick(const UiClick& click, EntityRef layout, Event<const UiClick&>& clicked, UiButtonEvents& buttons)
{
    // An empty ref matches nothing: a panel that has not spawned, or has been removed, is silent
    // rather than catching every other layout's presses.
    if (!layout || click.Layout != layout)
        return false;

    clicked.Raise(click);

    // Looked up after that raise, because a handler may have asked for a new Button event and
    // rehashed the map. The reference the node holds survives the rehash; an iterator would not.
    if (auto it = buttons.find(click.ButtonId); it != buttons.end())
        it->second.Raise(click.Slot);

    return true;
}

}  // namespace VoltMod::Internal
