#pragma once

#include <Color.h>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/Transmit.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace VoltMod
{

/**
 * @brief Per-viewer wallhack-style vision: one client sees live players as team-colored glow
 * outlines through walls, while every other client (and GOTV) never receives the glow entities.
 *
 * Each glowing player gets two prop_dynamic clones following their pawn - an invisible relay and
 * a glow prop parented to it (the indirection renders only the outline) - both transmit-filtered
 * to the beneficiary alone. Call @ref Reconcile on a repeating tick (see @ref ReconcileIntervalMs)
 * to track spawns, deaths, team/model changes, and round restarts; call @ref Destroy to tear down.
 */
/** Colors and the optional per-slot veto for a @ref GlowVision. A top-level type rather than a
 *  nested one so it is complete where GlowVision's constructor defaults it (GCC requires that). */
struct GlowConfig
{
    Color TerroristColor{255, 128, 0, 255};
    Color CtColor{0, 160, 255, 255};
    /** Extra per-slot veto on top of the built-in live/team/visibility checks (empty = all). */
    std::function<bool(int slot)> Filter;
};

class GlowVision
{
public:
    /** Suggested tick interval for @ref Reconcile. */
    static constexpr int ReconcileIntervalMs = 500;

    /** All three services must outlive this object; the Runtime owns them.
     *  `runtime.Visibility.CreateGlow(beneficiarySlot)` is the normal entry point - it passes
     *  `runtime.Entities`, `runtime.EntityOps` and `runtime.Transmit` for you. */
    GlowVision(EntitySystem& entities, EntityOps& ops, Transmit& transmit, int beneficiarySlot, GlowConfig config = {})
        : _entities(entities),
          _ops(ops),
          _transmit(transmit),
          _beneficiarySlot(beneficiarySlot),
          _config(std::move(config))
    {}

    /** Create/refresh/destroy glow clone pairs to match the current live players. */
    void Reconcile();

    /** Tear down all pairs (transmit-filter entries + surviving clone entities). */
    void Destroy();

private:
    struct GlowPair
    {
        EntityRef Relay;
        EntityRef Glow;
        int RelayIndex = -1;
        int GlowIndex = -1;
        int Team = 0;
        std::string Model;

        // The relay ref is the single source of truth for liveness; DestroyPair resets it.
        bool Active() const { return static_cast<bool>(Relay); }
    };

    void CreatePair(int slot, GlowPair& pair);
    void DestroyPair(GlowPair& pair);

    EntitySystem& _entities;
    EntityOps& _ops;
    Transmit& _transmit;
    int _beneficiarySlot;
    GlowConfig _config;
    std::array<GlowPair, MaxPlayers> _pairs{};
};

}  // namespace VoltMod
