#include <igameevents.h>

#include <VoltMod/Events/EventTypes.hpp>
#include <playerslot.h>

namespace VoltMod::Events
{

// GetPlayerSlot decodes the connection userid to the actual slot (userids drift from slots
// on reconnect); it yields -1 when the field is absent or holds no live player.

PlayerDeath PlayerDeath::From(IGameEvent& e)
{
    return {
        .VictimSlot = e.GetPlayerSlot("userid").Get(),
        .AttackerSlot = e.GetPlayerSlot("attacker").Get(),
        .Weapon = e.GetString("weapon", ""),
        .Headshot = e.GetBool("headshot"),
        .Penetrated = e.GetInt("penetrated"),
    };
}

PlayerSpawn PlayerSpawn::From(IGameEvent& e)
{
    return {.Slot = e.GetPlayerSlot("userid").Get()};
}

PlayerJump PlayerJump::From(IGameEvent& e)
{
    return {.Slot = e.GetPlayerSlot("userid").Get()};
}

PlayerHurt PlayerHurt::From(IGameEvent& e)
{
    return {
        .VictimSlot = e.GetPlayerSlot("userid").Get(),
        .AttackerSlot = e.GetPlayerSlot("attacker").Get(),
        .Health = e.GetInt("health"),
        .DamageHealth = e.GetInt("dmg_health"),
    };
}

PlayerBlind PlayerBlind::From(IGameEvent& e)
{
    return {
        .Slot = e.GetPlayerSlot("userid").Get(),
        .AttackerSlot = e.GetPlayerSlot("attacker").Get(),
        .BlindDuration = e.GetFloat("blind_duration"),
    };
}

PlayerTeam PlayerTeam::From(IGameEvent& e)
{
    return {
        .Slot = e.GetPlayerSlot("userid").Get(),
        .Team = e.GetInt("team"),
        .OldTeam = e.GetInt("oldteam"),
        .Disconnect = e.GetBool("disconnect"),
    };
}

PlayerConnectFull PlayerConnectFull::From(IGameEvent& e)
{
    return {.Slot = e.GetPlayerSlot("userid").Get()};
}

WeaponFire WeaponFire::From(IGameEvent& e)
{
    return {
        .Slot = e.GetPlayerSlot("userid").Get(),
        .Weapon = e.GetString("weapon", ""),
    };
}

BulletImpact BulletImpact::From(IGameEvent& e)
{
    return {
        .Slot = e.GetPlayerSlot("userid").Get(),
        .TruncatedUserId = e.GetInt("userid"),
        .X = e.GetFloat("x"),
        .Y = e.GetFloat("y"),
        .Z = e.GetFloat("z"),
    };
}

RoundStart RoundStart::From(IGameEvent&)
{
    return {};
}

RoundEnd RoundEnd::From(IGameEvent& e)
{
    return {
        .Winner = e.GetInt("winner"),
        .Reason = e.GetInt("reason"),
    };
}

RoundPrestart RoundPrestart::From(IGameEvent&)
{
    return {};
}

}  // namespace VoltMod::Events
