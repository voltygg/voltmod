#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Color.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/EntityRef.hpp>
#include <VoltMod/Engine/Math.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Schema/Generated/CBaseEntity.hpp>
#include <VoltMod/Schema/Generated/Enums.hpp>
// Flags_t: the FL_* bits Flags() returns.
#include <const.h>
#include <optional>
#include <string_view>

namespace VoltMod
{

/**
 * @brief A live entity, valid for this frame only.
 *
 * The engine can free it between frames: store an @ref EntityRef and resolve it again where you
 * use it. `if (entity)` is the validity check. A falsy entity reads zero and ignores writes and
 * verbs. Copies are cheap; assignment is deleted so a rebind never reads like a field write.
 *
 * Fields are generated pairs: `Health()` reads, `SetHealth(100)` writes and replicates.
 */
class Entity
{
protected:
    EntitySystem* _sys = nullptr;
    CEntityInstance* _e = nullptr;

public:
    Entity() = default;

    /** Prefer `runtime.Entities` lookups over building one by hand. */
    Entity(EntitySystem& entities, CEntityInstance* raw) noexcept : _sys(&entities), _e(raw) {}

    Entity(const Entity&) = default;
    Entity& operator=(const Entity&) = delete;

    explicit operator bool() const noexcept { return _e != nullptr; }

    /** The engine entity, for calls the framework does not wrap. */
    CEntityInstance* Raw() const noexcept { return _e; }

    /** Network index; -1 when falsy. */
    int Index() const;

    /** The handle to store; unset when falsy. */
    EntityRef Ref() const;

    /** Such as "player". Points into engine memory: copy it to keep it. */
    std::string_view ClassName() const;

    /** This entity as a player pawn; falsy for anything else, such as a prop or a grenade. */
    Pawn AsPawn() const;

    /** @name CBaseEntity fields */
    /** @{ */
#include <VoltMod/Schema/Generated/Wrappers/Entity.inc>
    /** @} */

    /** World position and rotation, from the scene node. */
    Vector Origin() const;
    QAngle Angles() const;

    /** Move the entity; std::nullopt leaves that part unchanged.
     *  @return Error::Unsupported when the Teleport slot did not bind. */
    Status Teleport(std::optional<Vector> origin, std::optional<QAngle> angles, std::optional<Vector> velocity) const;

    /** Spawn an entity from @ref EntitySystem::Create; the engine takes the keyvalues. */
    void Spawn(KeyValues& values) const;

    /** Fire an entity input, such as "Explode" or `("FollowEntity", "!activator", pawn)`. */
    void AcceptInput(std::string_view input, std::string_view value = {}, const Entity& activator = {}) const;

    /** Remove now. */
    void Remove() const;

    /** Remove after @p seconds, through the engine's input queue. */
    void RemoveAfter(float seconds) const;

    /** @name Model entities only
     *  Props, pawns, weapons and beams. On any other entity these write past the object. */
    /** @{ */
    void SetModel(std::string_view path) const;

    /** Render and collision scale, clamped to 0.05-3: larger values have crashed the server. */
    void SetScale(float scale) const;

    /** Write `m_nRenderMode` and `m_clrRender` together. `kRenderTransAlpha` makes the alpha show. */
    void SetRender(Schema::RenderMode_t mode, Color color) const;

    /** Play @p animation once from its first frame, then loop @p idle. */
    void PlayAnimation(std::string_view animation, std::string_view idle = {}) const;
    /** @} */

    /** Play a `.vsndevts` event from this entity, to everyone or to @p recipients. */
    void EmitSound(std::string_view soundEvent, float volume = 1.0f) const;
    void EmitSound(std::string_view soundEvent, IRecipientFilter& recipients, float volume = 1.0f) const;
};

}  // namespace VoltMod
