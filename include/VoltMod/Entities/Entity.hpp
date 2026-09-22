#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Math.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Schema/Generated/CBaseEntity.hpp>
// Flags_t: the FL_* bits Flags() returns.
#include <const.h>
#include <cstdint>
#include <optional>
#include <string_view>

namespace VoltMod
{

/**
 * A live entity. Frame-local: the engine can free it between frames, so do not keep an Entity,
 * Pawn or Controller across frames. Store an EntityRef (or PlayerRef) and resolve it again where
 * you use it. `if (entity)` checks validity; there is no IsValid().
 *
 * Fields are generated getter/setter pairs: `Health()` reads, `SetHealth(100)` writes and
 * replicates. A null wrapper reads zero and writes nothing.
 */
class Entity
{
protected:
    /** Null only when default-constructed. */
    EntitySystem* _sys = nullptr;
    CEntityInstance* _e = nullptr;

public:
    Entity() = default;

    /** Prefer runtime.Entities.Resolve/PawnOf/Controller over building one by hand. */
    Entity(EntitySystem& entities, CEntityInstance* raw) noexcept : _sys(&entities), _e(raw) {}

    Entity(const Entity&) = default;
    /** Not assignable: resolve a new wrapper instead. */
    Entity& operator=(const Entity&) = delete;

    explicit operator bool() const noexcept { return _e != nullptr; }

    /** The raw entity, for engine calls the framework does not wrap. */
    CEntityInstance* Raw() const noexcept { return _e; }

    /** Network entity index; -1 when null or unlinked. */
    int Index() const;

    /** The handle to store; invalid when null or unlinked. */
    EntityRef Ref() const;

    /** Classname such as "player"; empty when null. Points into engine memory: copy it to keep it. */
    std::string_view ClassName() const;

    /** @name CBaseEntity fields
     *  On a pawn use Pawn::SetMove, not SetMoveType: the engine reverts a lone move-type write next tick. */
    /** @{ */
#include <VoltMod/Schema/Generated/Wrappers/Entity.inc>
    /** @} */

    /** World position and rotation (from the scene node; not schema fields on CBaseEntity). */
    Vector Origin() const;
    QAngle Angles() const;

    /** Move the entity; a std::nullopt argument leaves that part unchanged.
     *  Errors: NotReady on a null entity, Unsupported when the Teleport vtable index did not bind. */
    Status Teleport(std::optional<Vector> origin, std::optional<QAngle> angles, std::optional<Vector> velocity) const;
};

}  // namespace VoltMod
