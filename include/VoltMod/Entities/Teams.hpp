#pragma once

namespace VoltMod
{

/** Engine team indices, as `m_iTeamNum` holds them. SDK-free so command parsing can share them. */
inline constexpr int TeamNone = 0;
inline constexpr int TeamSpectator = 1;
inline constexpr int TeamT = 2;
inline constexpr int TeamCT = 3;

}  // namespace VoltMod
