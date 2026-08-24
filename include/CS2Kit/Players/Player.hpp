#pragma once

#include <cstdint>
#include <string>

namespace CS2Kit::Players
{

/**
 * @brief Represents a connected player on the server.
 *
 * Tracks identity (slot, SteamID, name, IP) and connection time.
 * Plugin-specific state (admin flags, punishment cache, etc.) belongs in
 * separate plugin-side managers keyed by SteamID - not on this type.
 */
class Player
{
public:
    Player(int slot, int64_t steamId, const std::string& name, const std::string& ipAddress);

    int GetSlot() const { return _slot; }
    int64_t GetSteamID() const { return _steamId; }
    const std::string& GetName() const { return _name; }
    const std::string& GetIpAddress() const { return _ipAddress; }
    int64_t GetPlaytime() const;

    /** True for engine bots, which connect without a real SteamID. */
    bool IsBot() const { return _steamId == 0; }

private:
    int _slot;
    int64_t _steamId;
    std::string _name;
    std::string _ipAddress;
    int64_t _connectTime;
};

}  // namespace CS2Kit::Players
