#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/Controller.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace VoltMod
{

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

    /** What @p ref points at; falsy when it is unset, or its entity is gone or replaced. */
    Entity Resolve(EntityRef ref);

    /** First entity of @p className after @p after (a falsy one starts at the head); `*` wildcards
     *  match. Falsy when exhausted. */
    Entity FindByClassName(const Entity& after, std::string_view className);

    /** Unsupported when entities cannot be created or spawned. */
    Status Available() const;

    /** A new entity that has not spawned yet: set its fields, then call @ref Entity::Spawn. */
    Entity Create(std::string_view className);

    /** Create and spawn; falsy on failure. The engine takes the keyvalues. */
    Entity Spawn(std::string_view className, KeyValues& values);

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
