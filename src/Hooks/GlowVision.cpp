#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Hooks/GlowVision.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Schema/Generated/Enums.hpp>
#include <utility>

namespace VoltMod
{

// prop_dynamic keyvalues shared by the relay and glow clones.
static constexpr int PropSpawnFlags = 256;
static constexpr int GlowRangeUnits = 5000;
static constexpr int GlowTeamAny = -1;
static constexpr int GlowStateAlwaysOn = 3;
static constexpr int GlowRenderAmt = 1;

void GlowVision::DestroyPair(GlowPair& pair)
{
    if (!pair.Active())
    {
        return;
    }

    _visibility.ShowToEveryone(pair.Relay);
    _visibility.ShowToEveryone(pair.Glow);

    _entities.Resolve(pair.Glow).Remove();
    _entities.Resolve(pair.Relay).Remove();

    pair = {};
}

void GlowVision::CreatePair(int slot, GlowPair& pair)
{
    Pawn pawn = _entities.PawnOf(slot);
    if (!pawn)
    {
        return;
    }

    std::string model = pawn.ModelName();
    const Team team = pawn.Team();
    if (model.empty())
    {
        return;
    }

    KeyValues relayKv;
    relayKv.Set("model", model.c_str())
        .Set("spawnflags", PropSpawnFlags)
        .Set("rendermode", static_cast<int>(Schema::RenderMode_t::kRenderNone));
    const Entity relay = _entities.Spawn("prop_dynamic", relayKv);
    if (!relay)
    {
        return;
    }

    KeyValues glowKv;
    glowKv.Set("model", model.c_str())
        .Set("spawnflags", PropSpawnFlags)
        .Set("glowcolor", team == Team::T ? _config.TerroristColor : _config.CtColor)
        .Set("glowrange", GlowRangeUnits)
        .Set("glowteam", GlowTeamAny)
        .Set("glowstate", GlowStateAlwaysOn)
        .Set("renderamt", GlowRenderAmt);
    const Entity glow = _entities.Spawn("prop_dynamic", glowKv);
    if (!glow)
    {
        relay.Remove();
        return;
    }

    relay.AcceptInput("FollowEntity", "!activator", pawn);
    glow.AcceptInput("FollowEntity", "!activator", relay);

    pair.Relay = relay.Ref();
    pair.Glow = glow.Ref();
    pair.Team = team;
    pair.Model = std::move(model);

    _visibility.ShowOnlyTo(pair.Relay, _viewerSlot);
    _visibility.ShowOnlyTo(pair.Glow, _viewerSlot);
}

void GlowVision::Refresh()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        auto& pair = _pairs[slot];

        Pawn pawn = _entities.PawnOf(slot);
        const Team team = pawn.Team();
        // Hidden pawns never reach the viewer, so a clone would follow nothing.
        bool desired = slot != _viewerSlot && pawn && pawn.IsAlive() && IsPlaying(team) &&
                       !_visibility.IsPawnHidden(slot) && (!_config.Filter || _config.Filter(slot));

        if (pair.Active())
        {
            bool stale = !desired || team != pair.Team || !_entities.Resolve(pair.Relay) ||
                         !_entities.Resolve(pair.Glow) || pawn.ModelName() != pair.Model;
            if (stale)
            {
                DestroyPair(pair);
            }
        }

        if (!pair.Active() && desired)
        {
            CreatePair(slot, pair);
        }
    }
}

void GlowVision::Destroy()
{
    for (auto& pair : _pairs)
    {
        DestroyPair(pair);
    }
}

}  // namespace VoltMod
