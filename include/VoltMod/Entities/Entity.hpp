#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/Field.hpp>
#include <cstdint>
#include <optional>
#include <string_view>

namespace VoltMod
{

/** @defgroup EntityFlags CBaseEntity::m_fFlags bit values (Flags_t in the CS2 schema) */
/** @{ */
constexpr uint32_t FL_ONGROUND = 1;
constexpr uint32_t FL_DUCKING = 2;
constexpr uint32_t FL_FROZEN = 32;
constexpr uint32_t FL_FAKECLIENT = 256;
constexpr uint32_t FL_GODMODE = 16384;
constexpr uint32_t FL_NOTARGET = 32768;
/** @} */

/**
 * @brief A live entity, addressed by its fields.
 *
 * ### Validity
 *
 * Every wrapper built on this one - @ref Entity, @ref Pawn, @ref Controller - is a **frame-local
 * value**. It holds a raw entity pointer, and the engine frees entities between frames without
 * telling anyone, so a wrapper kept past the frame it was built in points at freed memory.
 *
 * - `explicit operator bool()` is the one validity check. There is no `IsValid()`.
 * - Never store a wrapper. Store an @ref EntityRef (any entity) or a @ref PlayerRef (a player) and
 *   resolve it again where it is needed: `runtime.Entities.Resolve(ref)`,
 *   `runtime.Entities.PawnOf(slot)`.
 * - Wrappers copy but do not assign. Copying rebinds a whole wrapper to the same entity, while
 *   assigning through a single @ref Field writes that field's value into the entity, and the two
 *   must not look alike at a call site.
 *
 * Reading a field of a falsy wrapper yields a zero value and writing it does nothing, so a
 * wrapper that was never resolved degrades rather than crashing. A stale non-null one does not,
 * which is why the rule is "never store one" and not "check before use".
 *
 * ### Fields
 *
 * Members declared as @ref Field are the entity's schema fields, used as if they were data
 * members. Writes to networked fields replicate on their own. `Entity` carries the CBaseEntity
 * fields every entity has; @ref Pawn and @ref Controller add theirs.
 */
class Entity
{
protected:
    /** These two are declared first because every Field below captures @ref _e in its initializer,
     *  and members initialize in declaration order. */

    /** The service graph the verbs below reach the engine through. Null only for a
     *  default-constructed wrapper, which is falsy anyway. */
    EntitySystem* _sys = nullptr;
    CEntityInstance* _e = nullptr;

public:
    Entity() = default;

    /** @p entities must outlive this wrapper - the Runtime owns it and the wrapper is frame-local.
     *  Prefer the factories on @ref EntitySystem over building one by hand. */
    Entity(EntitySystem& entities, CEntityInstance* raw) noexcept : _sys(&entities), _e(raw) {}

    Entity(const Entity&) = default;

    /** Wrappers are not assignable: see the validity note above. Re-resolve instead. */
    Entity& operator=(const Entity&) = delete;

    explicit operator bool() const noexcept { return _e != nullptr; }

    /** The entity this wraps, for the engine calls the framework has not typed yet. */
    [[nodiscard]] CEntityInstance* Raw() const noexcept { return _e; }

    /** Network entity index, or -1 when the entity is null or unlinked. */
    [[nodiscard]] int Index() const;

    /** The storable form of this entity. Invalid for a null or unlinked entity. */
    [[nodiscard]] EntityRef Ref() const;

    /** Designer classname, or empty. Borrowed from the engine; it dies with the entity. */
    [[nodiscard]] std::string_view ClassName() const;

    /** @name CBaseEntity fields, carried by every entity. */
    /** @{ */
    Field<int, "CBaseEntity", "m_iHealth"> Health{_e};
    Field<uint8_t, "CBaseEntity", "m_iTeamNum"> Team{_e};
    Field<uint8_t, "CBaseEntity", "m_lifeState"> LifeState{_e};
    Field<uint32_t, "CBaseEntity", "m_fFlags"> Flags{_e};
    Field<Vector, "CBaseEntity", "m_vecAbsVelocity"> Velocity{_e};
    /** Raw `m_MoveType`. On a pawn prefer @ref Pawn::Move and @ref Pawn::SetMove, which keep this
     *  and `m_nActualMoveType` in step - writing only one lets the engine revert it next tick. */
    Field<uint8_t, "CBaseEntity", "m_MoveType"> MoveTypeRaw{_e};
    Field<uint8_t, "CBaseEntity", "m_nActualMoveType"> ActualMoveTypeRaw{_e};
    /** @} */

    /** World position. Origin and rotation are not CBaseEntity schema fields in CS2; both live on
     *  the entity's CGameSceneNode, reached through `m_CBodyComponent`. */
    [[nodiscard]] Vector Origin() const;
    [[nodiscard]] QAngle Angles() const;

    /**
     * Move the entity through `CBaseEntity::Teleport`. A component left as `std::nullopt` is
     * unchanged.
     * @return Error::NotReady for a null entity, Error::Unsupported when the Teleport vtable index
     *         did not bind.
     */
    Status Teleport(std::optional<Vector> origin, std::optional<QAngle> angles, std::optional<Vector> velocity) const;
};

}  // namespace VoltMod
