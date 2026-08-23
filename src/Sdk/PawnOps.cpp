#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Players/PlayerManager.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/Entity.hpp>
#include <CS2Kit/Sdk/MoveType.hpp>
#include <CS2Kit/Sdk/PawnOps.hpp>
#include <cmath>
#include <mathlib/vector.h>
#include <numbers>
#include <random>

namespace CS2Kit::Sdk::PawnOps
{

namespace
{
float Rand(float lo, float hi)
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}
}  // namespace

Vector ClearedDestination(const PlayerController& anchor, float clearance)
{
    Vector origin = anchor.GetAbsOrigin();
    float yawRad = anchor.GetEyeAngles().y * std::numbers::pi_v<float> / 180.0f;
    origin.x += std::cos(yawRad) * clearance;
    origin.y += std::sin(yawRad) * clearance;
    return origin;
}

void SwapOrigins(const PlayerController& a, const PlayerController& b)
{
    // Read both before either move, since each origin is the other pawn's destination.
    Vector posA = a.GetAbsOrigin();
    Vector posB = b.GetAbsOrigin();
    Vector zero{0.0f, 0.0f, 0.0f};
    a.Teleport(&posB, nullptr, &zero);
    b.Teleport(&posA, nullptr, &zero);
}

void ShiftZ(const PlayerController& pc, float deltaZ)
{
    Vector origin = pc.GetAbsOrigin();
    origin.z += deltaZ;
    pc.Teleport(&origin, nullptr, nullptr);
}

bool ToggleNoclip(const PlayerController& pc)
{
    bool turningOn = (pc.GetMoveType() != MoveType::NoClip);
    pc.SetMoveType(turningOn ? MoveType::NoClip : MoveType::Walk);
    return turningOn;
}

bool ToggleFreeze(const PlayerController& pc)
{
    bool turningOn = (pc.GetMoveType() != MoveType::None);
    pc.SetMoveType(turningOn ? MoveType::None : MoveType::Walk);
    return turningOn;
}

bool HasGodmode(const PlayerController& pc)
{
    return (pc.GetFlags() & FL_GODMODE) != 0;
}

void SetGodmode(const PlayerController& pc, bool enable)
{
    uint32_t flags = pc.GetFlags();
    pc.SetFlags(enable ? (flags | FL_GODMODE) : (flags & ~FL_GODMODE));
}

bool ToggleGodmode(const PlayerController& pc)
{
    bool turningOn = !HasGodmode(pc);
    SetGodmode(pc, turningOn);
    return turningOn;
}

void Slap(const PlayerController& pc, float upward, float horizontal, int fallProtectMs)
{
    // Write velocity directly on the pawn rather than through the Teleport vfunc.
    // Teleport(nullptr origin, ...) was crashing the server in CS2 builds we tested;
    // m_vecAbsVelocity is the conventional path for velocity-only changes.
    pc.SetVelocity({Rand(-horizontal, horizontal), Rand(-horizontal, horizontal), upward});

    // Only toggle godmode for fall protection if the target wasn't already in godmode, otherwise
    // the delayed clear below would silently strip an externally applied godmode.
    if (fallProtectMs > 0 && !HasGodmode(pc))
    {
        SetGodmode(pc, true);

        // A ref, not the slot: the target can disconnect inside the protection window and the
        // next player to take the seat would have had their godmode stripped instead.
        auto& runtime = CS2Kit::Detail::Rt();
        Players::Player* player = runtime.Players.GetPlayerBySlot(pc.GetSlot());
        if (!player)
            return;

        runtime.Scheduler.Delay(fallProtectMs, [ref = player->Ref()]() {
            Players::Player* still = CS2Kit::Detail::Rt().Players.Resolve(ref);
            if (!still)
                return;

            PlayerController target(still->GetSlot());
            if (target.IsValid())
                SetGodmode(target, false);
        });
    }
}

bool ChangeTeamSafe(const PlayerController& pc, int team)
{
    if (team < TeamSpectator || team > TeamCT)
        return false;
    pc.ChangeTeam(team);
    return true;
}

}  // namespace CS2Kit::Sdk::PawnOps
