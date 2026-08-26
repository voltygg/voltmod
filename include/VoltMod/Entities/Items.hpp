#pragma once

#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/Pawn.hpp>

namespace VoltMod
{

/**
 * @brief Giving and stripping a player's weapons.
 *
 * Reaches CCSPlayer_ItemServices through the pawn's `m_pItemServices` and calls it by vtable
 * index, so the indices live in gamedata rather than in a signature. Every method resolves
 * what it needs on the call and returns false when the pawn, the service pointer, or the
 * index is unavailable - a missing index degrades the feature, it never crashes.
 *
 * Game-thread only.
 */
class Items
{
public:
    /** @p bindings must outlive this service; the Runtime declares it above. */
    explicit Items(const Bindings& bindings) : _bindings(bindings) {}
    Items(const Items&) = delete;
    Items& operator=(const Items&) = delete;

    /**
     * Give @p item (an entity classname, e.g. "weapon_ak47") to @p pawn.
     *
     * The engine refuses a weapon the player's team cannot buy, so a refusal is retried once
     * with the pawn temporarily on the other team. Both writes land in the same frame and the
     * original value is restored before this returns, so no client ever sees the flip - but it
     * does mean the call must not be interleaved with anything else that reads the pawn's team.
     *
     * @return false when the pawn is unavailable or the engine refused the item twice.
     */
    bool Give(const Pawn& pawn, const char* item);

    /**
     * Remove every weapon @p pawn is carrying.
     * @param removeSuit also strips armor and the defuse kit.
     */
    bool StripWeapons(const Pawn& pawn, bool removeSuit = true);

private:
    /** The pawn's CCSPlayer_ItemServices, or nullptr. */
    static void* ItemServices(const Pawn& pawn);

    const Bindings& _bindings;
};

}  // namespace VoltMod
