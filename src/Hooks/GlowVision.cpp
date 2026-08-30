#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Entities/PawnOps.hpp>
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

    if (Entity glow = _entities.Resolve(pair.Glow))
        _ops.Remove(glow.Raw());
    if (Entity relay = _entities.Resolve(pair.Relay))
        _ops.Remove(relay.Raw());

    pair = {};
}

void GlowVision::CreatePair(int slot, GlowPair& pair)
{
    Pawn pawn = _entities.PawnOf(slot);
    if (!pawn)
        return;

    std::string model = pawn.ModelName();
    const int team = pawn.Team();
    if (model.empty())
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

    _ops.AcceptInput(relay, "FollowEntity", "!activator", pawn.Raw());
    _ops.AcceptInput(glow, "FollowEntity", "!activator", relay);

    Entity relayEntity{_entities, relay};
    Entity glowEntity{_entities, glow};
    pair.Relay = relayEntity.Ref();
    pair.Glow = glowEntity.Ref();
    pair.RelayIndex = relayEntity.Index();
    pair.GlowIndex = glowEntity.Index();
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

        Pawn pawn = _entities.PawnOf(slot);
        const int team = pawn ? static_cast<int>(pawn.Team()) : 0;
        // Ghosted pawns never transmit to the beneficiary, so a clone would follow nothing.
        bool desired = slot != _beneficiarySlot && pawn && pawn.IsAlive() && (team == TeamT || team == TeamCT) &&
                       !_transmit.IsPawnHidden(slot) && (!_config.Filter || _config.Filter(slot));

        if (pair.Active())
        {
            bool stale = !desired || team != pair.Team || !_entities.Resolve(pair.Relay) ||
                         !_entities.Resolve(pair.Glow) || pawn.ModelName() != pair.Model;
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
