#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/EntityKeyValues.hpp>
#include <CS2Kit/Sdk/EntityOps.hpp>
#include <CS2Kit/Sdk/GlowVision.hpp>
#include <CS2Kit/Sdk/PawnOps.hpp>
#include <CS2Kit/Sdk/PlayerController.hpp>
#include <utility>

namespace CS2Kit::Sdk
{

namespace
{

constexpr int RenderModeNone = 10;

// prop_dynamic keyvalues shared by the relay and glow clones.
constexpr int PropSpawnFlags = 256;
constexpr int GlowRangeUnits = 5000;
constexpr int GlowTeamAny = -1;
constexpr int GlowStateAlwaysOn = 3;
constexpr int GlowRenderAmt = 1;

}  // namespace

void GlowVision::DestroyPair(GlowPair& pair)
{
    if (!pair.Active())
        return;

    // Unregister before removal: a recycled index still registered would filter
    // whatever entity the engine hands that index to next.
    auto& transmit = CS2Kit::Detail::Rt().Transmit;
    transmit.ClearEntityExclusive(pair.RelayIndex);
    transmit.ClearEntityExclusive(pair.GlowIndex);

    auto& ops = CS2Kit::Detail::Rt().EntityOps;
    auto& entities = CS2Kit::Detail::Rt().Entities;
    if (auto* glow = entities.ResolveEntityHandle(pair.GlowHandle))
        ops.Remove(glow);
    if (auto* relay = entities.ResolveEntityHandle(pair.RelayHandle))
        ops.Remove(relay);

    pair = {};
}

void GlowVision::CreatePair(int slot, GlowPair& pair)
{
    PlayerController pc(slot);
    auto* pawn = pc.GetPawn();
    std::string model = pc.GetPawnModelName();
    int team = pc.GetTeam();
    if (!pawn || model.empty())
        return;

    auto& ops = CS2Kit::Detail::Rt().EntityOps;

    EntityKeyValues relayKv;
    relayKv.Set("model", model.c_str()).Set("spawnflags", PropSpawnFlags).Set("rendermode", RenderModeNone);
    auto* relay = ops.Spawn("prop_dynamic", relayKv);
    if (!relay)
        return;

    EntityKeyValues glowKv;
    glowKv.Set("model", model.c_str())
        .Set("spawnflags", PropSpawnFlags)
        .Set("glowcolor", team == TeamT ? _config.TerroristColor : _config.CtColor)
        .Set("glowrange", GlowRangeUnits)
        .Set("glowteam", GlowTeamAny)
        .Set("glowstate", GlowStateAlwaysOn)
        .Set("renderamt", GlowRenderAmt);
    auto* glow = ops.Spawn("prop_dynamic", glowKv);
    if (!glow)
    {
        ops.Remove(relay);
        return;
    }

    ops.AcceptInput(relay, "FollowEntity", "!activator", pawn);
    ops.AcceptInput(glow, "FollowEntity", "!activator", relay);

    auto& entities = CS2Kit::Detail::Rt().Entities;
    pair.RelayHandle = entities.GetEntityHandle(relay);
    pair.GlowHandle = entities.GetEntityHandle(glow);
    pair.RelayIndex = entities.GetEntityIndex(relay);
    pair.GlowIndex = entities.GetEntityIndex(glow);
    pair.Team = team;
    pair.Model = std::move(model);

    auto& transmit = CS2Kit::Detail::Rt().Transmit;
    transmit.SetEntityExclusive(pair.RelayIndex, _beneficiarySlot);
    transmit.SetEntityExclusive(pair.GlowIndex, _beneficiarySlot);
}

void GlowVision::Reconcile()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        auto& pair = _pairs[slot];

        PlayerController pc(slot);
        int team = pc.GetTeam();
        // Ghosted pawns never transmit to the beneficiary, so a clone would follow nothing.
        bool desired = slot != _beneficiarySlot && pc.IsValid() && pc.IsAlive() && (team == TeamT || team == TeamCT) &&
                       !CS2Kit::Detail::Rt().Transmit.IsPawnHidden(slot) && (!_config.Filter || _config.Filter(slot));

        if (pair.Active())
        {
            auto& entities = CS2Kit::Detail::Rt().Entities;
            bool stale = !desired || team != pair.Team || !entities.ResolveEntityHandle(pair.RelayHandle) ||
                         !entities.ResolveEntityHandle(pair.GlowHandle) || pc.GetPawnModelName() != pair.Model;
            if (stale)
                DestroyPair(pair);
        }

        if (!pair.Active() && desired)
            CreatePair(slot, pair);
    }
}

void GlowVision::Destroy()
{
    for (auto& pair : _pairs)
        DestroyPair(pair);
}

}  // namespace CS2Kit::Sdk
