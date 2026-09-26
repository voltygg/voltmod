#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief One bullet landing, fired once per impact (so a shotgun blast produces several).
 *
 * Written by hand, not generated: the engine truncates this event's `userid` to its low byte, so it
 * does not round-trip to a slot. @ref ShooterSlot is a best-effort decode, -1 whenever the truncated
 * value names no live player. Correlate impacts by tick proximity (with @ref TruncatedUserId as a
 * disambiguator), never by identity alone.
 */
struct BulletImpact
{
    static constexpr std::string_view EventName = "bullet_impact";
    int ShooterSlot = -1;
    /** Raw `userid` as the engine sent it - the shooter's userid masked to its low byte. */
    int TruncatedUserId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    static BulletImpact From(IGameEvent& e);
};

}  // namespace VoltMod
