#include <CS2Kit/Core/CoreServices.hpp>
#include <CS2Kit/Players/PlayerManager.hpp>
#include <CS2Kit/Players/Roster.hpp>
#include <CS2Kit/Players/TargetResolver.hpp>
#include <CS2Kit/Sdk/PlayerController.hpp>
#include <random>

namespace CS2Kit::Players
{

std::expected<std::vector<Player*>, TargetFailure> ResolveTargets(std::string_view token, Player* caller,
                                                                  const TargetRules& rules,
                                                                  const CanTargetFn& canTarget)
{
    if (token.empty())
        return std::unexpected(TargetFailure{TargetError::NoMatch});

    auto& mgr = Players::Roster();
    const CanTargetFn& policy = canTarget ? canTarget : Core::Ctx().Policy.CanTarget;

    // Snapshot the roster into engine-free views; FilterRoster owns the grammar semantics.
    std::vector<PlayerView> roster;
    for (auto* p : mgr.GetAllPlayers())
    {
        if (!p)
            continue;
        Sdk::PlayerController ctrl = p->Controller();
        roster.push_back({
            .Slot = p->GetSlot(),
            .SteamId = p->GetSteamID(),
            .Name = p->GetName(),
            .Team = ctrl.IsValid() ? ctrl.GetTeam() : 0,
            .Alive = ctrl.IsValid() && ctrl.IsAlive(),
            .Bot = p->IsBot(),
            .Targetable = (caller && policy) ? policy(*caller, *p) : true,
        });
    }

    static std::mt19937 rng{std::random_device{}()};
    auto randomIndex = [](std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); };

    auto slots = FilterRoster(roster, ParseTargetToken(token), rules, caller ? caller->GetSlot() : -1, randomIndex);
    if (!slots)
        return std::unexpected(slots.error());

    std::vector<Player*> players;
    players.reserve(slots->size());
    for (int slot : *slots)
        if (auto* p = mgr.GetPlayerBySlot(slot))
            players.push_back(p);
    return players;
}

}  // namespace CS2Kit::Players
