#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Engine/Color.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Math.hpp>
#include <VoltMod/Entities/Controller.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** What @ref EntitySystem::SpawnProp places. */
struct PropSpec
{
    std::string_view Model;
    Vector Origin{0.0f, 0.0f, 0.0f};
    QAngle Angles{0.0f, 0.0f, 0.0f};
    /** False blocks nothing, bullets and traces included. */
    bool Solid = true;
    /** Material group; empty keeps the model's default. */
    std::string_view Skin;
    float Scale = 1.0f;
    bool CastsShadow = true;
    /** Set before it spawns, so a trace with @ref TraceOptions::IgnoreOwnedBy passes through it. */
    EntityRef Owner;
};

/**
 * @brief `runtime.Entities`: finds and creates entities. Everything it returns is valid for this
 * frame only; see @ref Entity.
 */
class EntitySystem
{
public:
    /** @p interfaces and @p bindings must outlive this service. */
    EntitySystem(VoltMod::Interfaces& interfaces, const VoltMod::Bindings& bindings);
    ~EntitySystem();
    EntitySystem(const EntitySystem&) = delete;
    EntitySystem& operator=(const EntitySystem&) = delete;

    /** The controller in @p slot; falsy when the slot is empty. */
    VoltMod::Controller Controller(int slot);

    /** The living body of @p slot's player; falsy when there is none. */
    VoltMod::Pawn Pawn(int slot);

    /** The player pawn @p ref points at; falsy when it is gone or not a player pawn. */
    VoltMod::Pawn Pawn(EntityRef ref);

    /** What @p ref points at; falsy when it is unset, or its entity is gone or replaced. */
    Entity Get(EntityRef ref);

    /** The first entity of @p className, such as "cs_gamerules"; `*` wildcards match. */
    Entity Find(std::string_view className);

    /** Every entity of @p className, taken now, so removing them while looping is safe. */
    std::vector<Entity> FindAll(std::string_view className);

    /** Every living player's pawn, in slot order. */
    std::vector<VoltMod::Pawn> AlivePawns();

    /** Unsupported when entities cannot be created or spawned. */
    Status Available() const;

    /** A new entity that has not spawned yet: set its fields, then call @ref Entity::Spawn. */
    Entity Create(std::string_view className);

    /** Create and spawn; falsy on failure. The engine takes the keyvalues. */
    Entity Spawn(std::string_view className, KeyValues& values);

    /** A `prop_dynamic`; falsy when it did not spawn. */
    Entity SpawnProp(const PropSpec& prop);

    /** A running particle effect such as "particles/explosion.vpcf"; empty for an empty name. Remove it to stop it. */
    Entity SpawnParticle(std::string_view effect, const Vector& origin, const QAngle& angles = {0.0f, 0.0f, 0.0f});

    /** A straight line of @p width from @p from to @p to. */
    Entity SpawnBeam(const Vector& from, const Vector& to, float width, Color color);

    /** @name Framework plumbing */
    /** @{ */
    /** An error only when entity lookups can never work; a missing system before the first map is
     *  fine, and @ref OnServerStartup picks it up. */
    Status Initialize();

    /** Re-read the system for the new map. */
    void OnServerStartup();

    /** The engine's entity system; null before the first map. */
    CGameEntitySystem* Raw();

    /** What the wrappers' verbs call the engine through. */
    const VoltMod::Bindings& Bindings() const noexcept { return _bindings; }
    const VoltMod::Interfaces& Interfaces() const noexcept { return _interfaces; }
    /** @} */

private:
    CEntityInstance* RawController(int slot);

    /** Null when the gamedata offset does not reach a CGameEntitySystem. */
    CGameEntitySystem* ReadEntitySystemPointer();

    /** Also publishes it for the SDK's ::GameEntitySystem(). */
    void SetEntitySystem(CGameEntitySystem* system);

    VoltMod::Interfaces& _interfaces;
    const VoltMod::Bindings& _bindings;
    /** Empty until the first pointer is checked. */
    std::optional<bool> _isRealSystem;
};

}  // namespace VoltMod
