#pragma once

namespace VoltMod::Engine
{
class GameData;
}  // namespace VoltMod::Engine

namespace VoltMod::Entities
{

class PlayerController;
class SchemaService;  // Internal type (src/Entities/Schema.hpp), kept out of the public graph.

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
    /** Both must outlive this service; the Runtime declares them above it. */
    Items(Engine::GameData& gameData, SchemaService& schema);
    Items(const Items&) = delete;
    Items& operator=(const Items&) = delete;

    /**
     * Give @p item (an entity classname, e.g. "weapon_ak47") to @p pc.
     *
     * The engine refuses a weapon the player's team cannot buy, so a refusal is retried once
     * with the pawn temporarily on the other team. That flip is not networked - it is undone
     * before this returns - but it does mean the call must not be interleaved with anything
     * else that reads the pawn's team.
     *
     * @return false when the pawn is unavailable or the engine refused the item twice.
     */
    bool Give(const PlayerController& pc, const char* item);

    /**
     * Remove every weapon @p pc is carrying.
     * @param removeSuit also strips armor and the defuse kit.
     */
    bool StripWeapons(const PlayerController& pc, bool removeSuit = true);

private:
    /** The pawn's CCSPlayer_ItemServices, or nullptr. */
    void* ItemServices(const PlayerController& pc) const;

    Engine::GameData& _gameData;
    SchemaService& _schema;
};

}  // namespace VoltMod::Entities
