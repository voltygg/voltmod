#include <VoltMod/Players/PlayerManager.hpp>
#include <algorithm>
#include <utility>

namespace VoltMod
{

// Bots share SteamID 0, so indexing them would make the map's single 0 entry mean whichever
// bot connected last - and removing any one of them would unindex a live player. Get(slot) is
// the only sensible lookup for a bot.
void PlayerManager::IndexBySteamId(Player* player)
{
    if (!player->IsBot())
        _playersBySteamId[player->SteamId()] = player;
}

void PlayerManager::UnindexBySteamId(const Player* player)
{
    if (player->IsBot())
        return;

    // Only drop the entry if it still points at this player: a reconnect under the same SteamID
    // into a different slot must not be unindexed by the old occupant leaving.
    auto it = _playersBySteamId.find(player->SteamId());
    if (it != _playersBySteamId.end() && it->second == player)
        _playersBySteamId.erase(it);
}

void PlayerManager::Reindex()
{
    _ordered.clear();
    _ordered.reserve(_playersBySlot.size());
    for (const auto& [slot, player] : _playersBySlot)
        _ordered.push_back(player.get());
    std::sort(_ordered.begin(), _ordered.end(), [](const Player* a, const Player* b) { return a->Slot() < b->Slot(); });
}

void PlayerManager::Drop(int slot)
{
    auto it = _playersBySlot.find(slot);
    if (it == _playersBySlot.end())
        return;

    // Raised while the player is still in the roster, so a handler may still look them up.
    Disconnected.Raise(*it->second);

    // Re-find: a handler is allowed to touch the roster, and the node may have moved or gone.
    it = _playersBySlot.find(slot);
    if (it == _playersBySlot.end())
        return;

    UnindexBySteamId(it->second.get());
    _playersBySlot.erase(it);
    Reindex();
}

Player* PlayerManager::Add(int slot, int64_t steamId, std::string name, std::string ip)
{
    // A slot can be re-occupied without a disconnect ever reaching us. The previous occupant
    // leaves the same way any other does, or the SteamID map keeps a dangling pointer and
    // whatever a plugin keyed on them is never released.
    Drop(slot);

    auto player = std::make_unique<Player>(slot, steamId, std::move(name), std::move(ip), _entities);
    Player* playerPtr = player.get();

    _playersBySlot[slot] = std::move(player);
    IndexBySteamId(playerPtr);
    Reindex();

    // Slot change first: it value-resets the per-slot caches, so a Connected handler that writes
    // per-slot state is not wiped a moment later.
    _slots.Raise(slot);
    Connected.Raise(*playerPtr);
    return playerPtr;
}

void PlayerManager::Remove(int slot)
{
    if (!_playersBySlot.contains(slot))
        return;

    Drop(slot);
    _slots.Raise(slot);
}

void PlayerManager::Clear()
{
    std::vector<int> slots;
    slots.reserve(_playersBySlot.size());
    for (const auto& [slot, player] : _playersBySlot)
        slots.push_back(slot);
    std::sort(slots.begin(), slots.end());

    for (int slot : slots)
        Drop(slot);

    // Anything left is a player a Disconnected handler added; drop it without a second raise.
    _playersBySlot.clear();
    _playersBySteamId.clear();
    _ordered.clear();

    for (int slot : slots)
        _slots.Raise(slot);
}

void PlayerManager::OnClientFullyConnected(int slot)
{
    if (Player* player = Get(slot))
        FullyConnected.Raise(*player);
}

void PlayerManager::OnClientSettingsChanged(int slot)
{
    if (Player* player = Get(slot))
        SettingsChanged.Raise(*player);
}

Player* PlayerManager::Get(int slot)
{
    auto it = _playersBySlot.find(slot);
    return it != _playersBySlot.end() ? it->second.get() : nullptr;
}

Player* PlayerManager::Get(PlayerRef ref)
{
    Player* player = Get(ref.Slot);
    return (player && player->SteamId() == ref.SteamId) ? player : nullptr;
}

Player* PlayerManager::BySteamId(int64_t steamId)
{
    auto it = _playersBySteamId.find(steamId);
    return it != _playersBySteamId.end() ? it->second : nullptr;
}

PlayerRef PlayerManager::RefFor(int slot)
{
    Player* player = Get(slot);
    return player ? player->Ref() : PlayerRef{};
}

}  // namespace VoltMod
