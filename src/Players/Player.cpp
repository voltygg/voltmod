#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Players/Player.hpp>

namespace VoltMod
{

VoltMod::Controller Player::Controller() const
{
    return _entities ? _entities->Controller(_slot) : VoltMod::Controller{};
}

VoltMod::Pawn Player::Pawn() const
{
    return _entities ? _entities->Pawn(_slot) : VoltMod::Pawn{};
}

std::string Player::Name() const
{
    if (_entities)
    {
        // The controller carries the live scoreboard name. It is empty before the player has a
        // controller, and the engine also reports an empty name for a moment around connect.
        if (const VoltMod::Controller controller = _entities->Controller(_slot))
        {
            std::string live(controller.Name());
            if (!live.empty())
            {
                return live;
            }
        }
    }
    return _connectName;
}

}  // namespace VoltMod
