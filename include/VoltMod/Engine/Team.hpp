#pragma once

#include <cstdint>

namespace VoltMod
{

/** Engine team indices, as `m_iTeamNum` holds them. SDK-free so command parsing can share them. */
enum class Team : uint8_t
{
    None = 0,
    Spectator = 1,
    T = 2,
    CT = 3,
};

/** True for the two teams that play a round. */
constexpr bool IsPlaying(Team team)
{
    return team == Team::T || team == Team::CT;
}

/** The other playing team; @ref Team::None for a team that does not play. */
constexpr Team Opposite(Team team)
{
    switch (team)
    {
    case Team::T:
        return Team::CT;
    case Team::CT:
        return Team::T;
    default:
        return Team::None;
    }
}

}  // namespace VoltMod
