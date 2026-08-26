#include <VoltMod/Players/PlayerManager.hpp>

namespace VoltMod
{

// Bots share SteamID 0, so indexing them would make the map's single 0 entry mean whichever
// bot connected last - and removing any one of them would unindex a live player. GetPlayerBySlot
// is the only sensible lookup for a bot.
void PlayerManager::IndexBySteamId(Player* player)
{
    if (!player->IsBot())
        _playersBySteamId[player->GetSteamID()] = player;
}

void PlayerManager::UnindexBySteamId(const Player* player)
{
    if (player->IsBot())
        return;

    // Only drop the entry if it still points at this player: a reconnect under the same SteamID
    // into a different slot must not be unindexed by the old occupant leaving.
    auto it = _playersBySteamId.find(player->GetSteamID());
    if (it != _playersBySteamId.end() && it->second == player)
        _playersBySteamId.erase(it);
}

Player* PlayerManager::AddPlayer(int slot, int64_t steamId, const std::string& name, const std::string& ipAddress)
{
    // A slot can be re-occupied without a disconnect ever reaching us. Unindex the previous
    // occupant before its unique_ptr drops, or the SteamID map keeps a dangling pointer.
    if (auto prev = _playersBySlot.find(slot); prev != _playersBySlot.end())
    {
        UnindexBySteamId(prev->second.get());
        _playersBySlot.erase(prev);
    }

    auto player = std::make_unique<Player>(slot, steamId, name, ipAddress);
    Player* playerPtr = player.get();

    _playersBySlot[slot] = std::move(player);
    IndexBySteamId(playerPtr);

    FireSlotChange(slot);
    return playerPtr;
}

void PlayerManager::RemovePlayer(int slot)
{
    auto it = _playersBySlot.find(slot);
    if (it != _playersBySlot.end())
    {
        UnindexBySteamId(it->second.get());
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

}  // namespace VoltMod
