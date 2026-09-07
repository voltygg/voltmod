#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Players/Player.hpp>

namespace VoltMod
{

Controller Player::Ctrl() const
{
    return _entities ? _entities->Controller(_slot) : Controller{};
}

Pawn Player::GetPawn() const
{
    return _entities ? _entities->PawnOf(_slot) : Pawn{};
}

std::string Player::Name() const
{
    if (_entities)
    {
        // The controller carries the live scoreboard name. It is empty before the player has a
        // controller, and the engine also reports an empty name for a moment around connect.
        if (Controller controller = _entities->Controller(_slot))
        {
            std::string live(controller.Name());
            if (!live.empty())
                return live;
        }
    }
    return _connectName;
}

}  // namespace VoltMod
