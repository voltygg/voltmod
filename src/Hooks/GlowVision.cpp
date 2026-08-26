#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Entities/PawnOps.hpp>
#include <VoltMod/Entities/PlayerController.hpp>
#include <VoltMod/Hooks/GlowVision.hpp>
#include <VoltMod/Hooks/Transmit.hpp>
#include <utility>

namespace VoltMod
{

static constexpr int RenderModeNone = 10;

// prop_dynamic keyvalues shared by the relay and glow clones.
static constexpr int PropSpawnFlags = 256;
static constexpr int GlowRangeUnits = 5000;
static constexpr int GlowTeamAny = -1;
static constexpr int GlowStateAlwaysOn = 3;
static constexpr int GlowRenderAmt = 1;

void GlowVision::DestroyPair(GlowPair& pair)
{
    if (!pair.Active())
        return;

    // Unregister before removal: a recycled index still registered would filter
    // whatever entity the engine hands that index to next.
    _transmit.ClearEntityExclusive(pair.RelayIndex);
    _transmit.ClearEntityExclusive(pair.GlowIndex);

    if (auto* glow = _entities.ResolveEntityHandle(pair.GlowHandle))
        _ops.Remove(glow);
    if (auto* relay = _entities.ResolveEntityHandle(pair.RelayHandle))
        _ops.Remove(relay);

    pair = {};
}

void GlowVision::CreatePair(int slot, GlowPair& pair)
{
    PlayerController pc = _entities.Controller(slot);
    auto* pawn = pc.GetPawn();
    std::string model = pc.GetPawnModelName();
    int team = pc.GetTeam();
    if (!pawn || model.empty())
        return;

    KeyValues relayKv;
    relayKv.Set("model", model.c_str()).Set("spawnflags", PropSpawnFlags).Set("rendermode", RenderModeNone);
    auto* relay = _ops.Spawn("prop_dynamic", relayKv);
    if (!relay)
        return;

    KeyValues glowKv;
    glowKv.Set("model", model.c_str())
        .Set("spawnflags", PropSpawnFlags)
        .Set("glowcolor", team == TeamT ? _config.TerroristColor : _config.CtColor)
        .Set("glowrange", GlowRangeUnits)
        .Set("glowteam", GlowTeamAny)
        .Set("glowstate", GlowStateAlwaysOn)
        .Set("renderamt", GlowRenderAmt);
    auto* glow = _ops.Spawn("prop_dynamic", glowKv);
    if (!glow)
    {
        _ops.Remove(relay);
        return;
    }

    _ops.AcceptInput(relay, "FollowEntity", "!activator", pawn);
    _ops.AcceptInput(glow, "FollowEntity", "!activator", relay);

    pair.RelayHandle = _entities.GetEntityHandle(relay);
    pair.GlowHandle = _entities.GetEntityHandle(glow);
    pair.RelayIndex = _entities.GetEntityIndex(relay);
    pair.GlowIndex = _entities.GetEntityIndex(glow);
    pair.Team = team;
    pair.Model = std::move(model);

    _transmit.SetEntityExclusive(pair.RelayIndex, _beneficiarySlot);
    _transmit.SetEntityExclusive(pair.GlowIndex, _beneficiarySlot);
}

void GlowVision::Reconcile()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        auto& pair = _pairs[slot];

        PlayerController pc = _entities.Controller(slot);
        int team = pc.GetTeam();
        // Ghosted pawns never transmit to the beneficiary, so a clone would follow nothing.
        bool desired = slot != _beneficiarySlot && pc.IsValid() && pc.IsAlive() && (team == TeamT || team == TeamCT) &&
                       !_transmit.IsPawnHidden(slot) && (!_config.Filter || _config.Filter(slot));

        if (pair.Active())
        {
            bool stale = !desired || team != pair.Team || !_entities.ResolveEntityHandle(pair.RelayHandle) ||
                         !_entities.ResolveEntityHandle(pair.GlowHandle) || pc.GetPawnModelName() != pair.Model;
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

}  // namespace VoltMod
