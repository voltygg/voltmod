#include <CS2Kit/Core/ActiveService.hpp>
#include <CS2Kit/Players/Roster.hpp>

namespace CS2Kit::Players
{

void SetActiveRoster(PlayerManager* roster)
{
    Core::ActiveService<PlayerManager>::Set(roster);
}

PlayerManager& Roster()
{
    return Core::ActiveService<PlayerManager>::Get();
}

PlayerManager* RosterOrNull()
{
    return Core::ActiveService<PlayerManager>::GetOrNull();
}

}  // namespace CS2Kit::Players
