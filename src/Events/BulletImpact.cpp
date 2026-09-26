#include <igameevents.h>

#include <VoltMod/Events/BulletImpact.hpp>
#include <playerslot.h>

namespace VoltMod
{

BulletImpact BulletImpact::From(IGameEvent& e)
{
    return {
        .ShooterSlot = e.GetPlayerSlot("userid").Get(),
        .TruncatedUserId = e.GetInt("userid"),
        .X = e.GetFloat("x"),
        .Y = e.GetFloat("y"),
        .Z = e.GetFloat("z"),
    };
}

}  // namespace VoltMod
