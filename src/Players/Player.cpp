#include <VoltMod/Core/TimeUtils.hpp>
#include <VoltMod/Players/Player.hpp>

namespace VoltMod::Players
{

using namespace VoltMod::Core;

Player::Player(int slot, int64_t steamId, const std::string& name, const std::string& ipAddress)
    : _slot(slot), _steamId(steamId), _name(name), _ipAddress(ipAddress), _connectTime(TimeUtils::Now())
{}

int64_t Player::GetPlaytime() const
{
    return TimeUtils::Now() - _connectTime;
}

}  // namespace VoltMod::Players
