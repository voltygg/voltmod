#include <CS2Kit/Players/PlayerManager.hpp>
#include <CS2Kit/Utils/StringUtils.hpp>

namespace CS2Kit::Players
{

using namespace CS2Kit::Utils;

Player* PlayerManager::AddPlayer(int slot, int64_t steamId, const std::string& name, const std::string& ipAddress)
{
    _playersBySlot.erase(slot);

    auto player = std::make_unique<Player>(slot, steamId, name, ipAddress);
    Player* playerPtr = player.get();

    _playersBySlot[slot] = std::move(player);
    _playersBySteamId[steamId] = playerPtr;

    FireSlotChange(slot);
    return playerPtr;
}

void PlayerManager::RemovePlayer(int slot)
{
    auto it = _playersBySlot.find(slot);
    if (it != _playersBySlot.end())
    {
        _playersBySteamId.erase(it->second->GetSteamID());
        _playersBySlot.erase(it);
        FireSlotChange(slot);
    }
}

void PlayerManager::FireSlotChange(int slot)
{
    _slots.Raise(slot);
}

Player* PlayerManager::GetPlayerBySlot(int slot)
{
    auto it = _playersBySlot.find(slot);
    if (it != _playersBySlot.end())
    {
        return it->second.get();
    }

    return nullptr;
}

Player* PlayerManager::GetPlayerBySteamId(int64_t steamId)
{
    auto it = _playersBySteamId.find(steamId);
    if (it != _playersBySteamId.end())
    {
        return it->second;
    }

    return nullptr;
}

Player* PlayerManager::GetPlayerBySlotIfSteamId(int slot, int64_t steamId)
{
    Player* player = GetPlayerBySlot(slot);
    return (player && player->GetSteamID() == steamId) ? player : nullptr;
}

std::vector<Player*> PlayerManager::FindPlayersByName(const std::string& name)
{
    std::vector<Player*> results;

    for (const auto& [slot, player] : _playersBySlot)
    {
        if (StringUtils::ContainsIgnoreCase(player->GetName(), name))
        {
            results.push_back(player.get());
        }
    }

    return results;
}

std::vector<Player*> PlayerManager::GetAllPlayers()
{
    std::vector<Player*> players;
    players.reserve(_playersBySlot.size());

    for (const auto& [slot, player] : _playersBySlot)
    {
        players.push_back(player.get());
    }

    return players;
}

size_t PlayerManager::GetPlayerCount() const
{
    return _playersBySlot.size();
}

void PlayerManager::Clear()
{
    std::vector<int> slots;
    slots.reserve(_playersBySlot.size());
    for (const auto& [slot, player] : _playersBySlot)
        slots.push_back(slot);

    _playersBySlot.clear();
    _playersBySteamId.clear();

    for (int slot : slots)
        FireSlotChange(slot);
}

}  // namespace CS2Kit::Players
