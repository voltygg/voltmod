#include <igameevents.h>

#include <VoltMod/Events/EventTypes.hpp>
#include <gametrace.h>
#include <playerslot.h>
#include <utility>

namespace VoltMod
{

static_assert(std::to_underlying(HitGroup::Invalid) == HITGROUP_INVALID);
static_assert(std::to_underlying(HitGroup::Generic) == HITGROUP_GENERIC);
static_assert(std::to_underlying(HitGroup::Head) == HITGROUP_HEAD);
static_assert(std::to_underlying(HitGroup::Chest) == HITGROUP_CHEST);
static_assert(std::to_underlying(HitGroup::Stomach) == HITGROUP_STOMACH);
static_assert(std::to_underlying(HitGroup::LeftArm) == HITGROUP_LEFTARM);
static_assert(std::to_underlying(HitGroup::RightArm) == HITGROUP_RIGHTARM);
static_assert(std::to_underlying(HitGroup::LeftLeg) == HITGROUP_LEFTLEG);
static_assert(std::to_underlying(HitGroup::RightLeg) == HITGROUP_RIGHTLEG);
static_assert(std::to_underlying(HitGroup::Neck) == HITGROUP_NECK);

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
        .Weapon = e.GetString("weapon", ""),
        .Health = e.GetInt("health"),
        .DamageHealth = e.GetInt("dmg_health"),
        .Hitbox = static_cast<HitGroup>(e.GetInt("hitgroup", static_cast<int>(HitGroup::Generic))),
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
        .Team = static_cast<VoltMod::Team>(e.GetInt("team")),
        .OldTeam = static_cast<VoltMod::Team>(e.GetInt("oldteam")),
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

VoteCast VoteCast::From(IGameEvent& e)
{
    return {
        .Slot = e.GetPlayerSlot("userid").Get(),
        .Option = e.GetInt("vote_option"),
    };
}

}  // namespace VoltMod
