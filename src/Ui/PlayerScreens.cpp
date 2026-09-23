#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Ui/PlayerScreens.hpp>
#include <utility>

namespace VoltMod
{

PlayerScreens::PlayerScreens(ScreenManager& screens, std::string layout) : _screens(screens), _layout(std::move(layout))
{}

Screen& PlayerScreens::For(int slot)
{
    if (!IsValidSlot(slot))
    {
        return _empty;
    }

    std::optional<Screen>& screen = _created[slot];
    if (!screen)
    {
        auto created = _screens.ForPlayer(_layout, slot);
        // Kept empty rather than retried: what ForPlayer refuses for lasts the whole load.
        if (!created)
        {
            Log::Warn("Screen '{}': no player screen for slot {} ({}).", _layout, slot, created.error().Detail);
        }
        screen = created ? std::move(*created) : Screen();
    }
    return *screen;
}

Screen* PlayerScreens::Find(int slot)
{
    if (!IsValidSlot(slot) || !_created[slot])
    {
        return nullptr;
    }
    return &*_created[slot];
}

}  // namespace VoltMod
