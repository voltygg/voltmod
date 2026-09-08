#pragma once

#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <array>
#include <string>
#include <utility>

namespace VoltMod
{

/**
 * @brief Per-viewer wallhack-style vision: one client sees live players as team-colored glow
 * outlines through walls, while every other client (and GOTV) never receives the glow entities.
 *
 * Each glowing player gets two prop_dynamic clones following their pawn - an invisible relay and
 * a glow prop parented to it (the indirection renders only the outline) - both shown to the
 * viewer alone. Call @ref Refresh on a repeating tick (see @ref RefreshIntervalMs) to track
 * spawns, deaths, team/model changes, and round restarts; call @ref Destroy to remove it.
 */
class GlowVision
{
public:
    /** Suggested tick interval for @ref Refresh. */
    static constexpr int RefreshIntervalMs = 500;

    /** All three services must outlive this object; `runtime.Hooks.Visibility.CreateGlow(slot)`
     *  is the normal entry point and passes them for you. */
    GlowVision(EntitySystem& entities, EntityOps& ops, Visibility& visibility, int viewerSlot,
               GlowConfig config = {})
        : _entities(entities),
          _ops(ops),
          _visibility(visibility),
          _viewerSlot(viewerSlot),
          _config(std::move(config))
    {}

    /** Create/refresh/destroy glow clone pairs to match the current live players. */
    void Refresh();

    /** Remove all private-entity entries and surviving clone entities. */
    void Destroy();

private:
    struct GlowPair
    {
        EntityRef Relay;
        EntityRef Glow;
        int Team = 0;
        std::string Model;

        // The relay ref is the single source of truth for liveness; DestroyPair resets it.
        bool Active() const { return static_cast<bool>(Relay); }
    };

    void CreatePair(int slot, GlowPair& pair);
    void DestroyPair(GlowPair& pair);

    EntitySystem& _entities;
    EntityOps& _ops;
    Visibility& _visibility;
    int _viewerSlot;
    GlowConfig _config;
    std::array<GlowPair, MaxPlayers> _pairs{};
};

}  // namespace VoltMod
