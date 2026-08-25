#pragma once

#include <VoltMod/Sdk/Engine/MemoryAccess.hpp>
#include <VoltMod/Sdk/Entity/MoveType.hpp>
#include <VoltMod/Sdk/Entity/ObserverMode.hpp>
#include <cstdint>
#include <string>

class CEntityInstance;
class Vector;
class QAngle;

namespace VoltMod::Sdk
{

class EntitySystem;

/**
 * @brief Transient engine-side player wrapper around CCSPlayerController.
 * Provides typed access to entity fields via the schema system,
 * engine operations (kick), and vtable operations (slay, change team, respawn).
 */
class PlayerController
{
public:
    /**
     * Resolve the controller in @p slot. Prefer the factory `runtime.Entities.Controller(slot)`,
     * which supplies @p entities for you. @p entities must outlive this wrapper - the Runtime owns
     * it - and the wrapper itself caches the controller pointer, so treat it as a value good for
     * the current frame rather than stored state.
     */
    PlayerController(EntitySystem& entities, int slot);

    bool IsValid() const;
    CEntityInstance* GetEntity() const;
    CEntityInstance* GetPawn() const;
    int GetSlot() const { return _slot; }

    void Kick(const char* reason) const;

    // Raw schema-field access (escape hatch). Header-only so any field type works without
    // editing this file; the offset lookup is delegated to SchemaOffset() to keep the internal
    // SchemaService out of this public header.
    template <typename T>
    T GetField(const char* className, const char* fieldName) const
    {
        if (!_controller)
            return T{};
        int offset = SchemaOffset(className, fieldName, sizeof(T));
        if (offset < 0)
            return T{};
        return ReadAt<T>(_controller, offset);
    }

    /** @p expectedSize 0 skips the drift check, for a T read out of a larger wrapper field. */
    template <typename T>
    T GetPawnField(const char* className, const char* fieldName, int expectedSize = sizeof(T)) const
    {
        auto* pawn = GetPawn();
        if (!pawn)
            return T{};
        int offset = SchemaOffset(className, fieldName, expectedSize);
        if (offset < 0)
            return T{};
        return ReadAt<T>(pawn, offset);
    }

    template <typename T>
    void SetField(const char* className, const char* fieldName, const T& value) const
    {
        if (!_controller)
            return;
        int offset = SchemaOffset(className, fieldName, sizeof(T));
        if (offset < 0)
            return;
        WriteAt<T>(_controller, offset, value);
    }

    template <typename T>
    void SetPawnField(const char* className, const char* fieldName, const T& value) const
    {
        auto* pawn = GetPawn();
        if (!pawn)
            return;
        int offset = SchemaOffset(className, fieldName, sizeof(T));
        if (offset < 0)
            return;
        WriteAt<T>(pawn, offset, value);
    }

    int GetHealth() const;
    void SetHealth(int health) const;
    int GetTeam() const;
    int GetLifeState() const;
    bool IsAlive() const;
    uint64_t GetButtons() const;
    int GetArmor() const;
    void SetArmor(int armor) const;

    /** Write the pawn's movement-speed multiplier (CCSPlayerPawn::m_flVelocityModifier).
     *  1.0 is normal speed. Note the game decays this toward 1.0 (e.g. after firing). */
    void SetSpeedModifier(float multiplier) const;

    /** CBaseEntity::m_fFlags bitmask (FL_* values in Entity.hpp). */
    uint32_t GetFlags() const;
    void SetFlags(uint32_t flags) const;

    Vector GetVelocity() const;
    void SetVelocity(const Vector& velocity) const;

    /** Pawn render mode/color (CBaseModelEntity::m_nRenderMode / m_clrRender). */
    uint8_t GetRenderMode() const;
    uint32_t GetRenderColor() const;
    void SetRender(uint8_t mode, uint32_t color) const;

    Vector GetAbsOrigin() const;
    QAngle GetAbsAngles() const;
    QAngle GetEyeAngles() const;

    /** Pawn abs origin plus its view offset (m_vecViewOffset) - where shots originate. */
    Vector GetEyePosition() const;

    /** CCSPlayerPawnBase::m_flFlashDuration - remaining full-blind fade seconds set by the last flash. */
    float GetFlashDuration() const;

    /** CCSPlayerPawnBase::m_flFlashMaxAlpha - 255 means the last flash was a full blind. */
    float GetFlashMaxAlpha() const;

    void Slay() const;
    void ChangeTeam(int team) const;
    void Respawn() const;

    /** Read the pawn's current `m_MoveType`. */
    MoveType GetMoveType() const;

    /** Write both `m_MoveType` and `m_nActualMoveType` (both must be set or the engine reverts it next tick). */
    void SetMoveType(MoveType type) const;

    /** Read `m_iObserverMode` from the pawn's CPlayer_ObserverServices. */
    ObserverMode_t GetObserverMode() const;

    /** Write `m_iObserverMode` on the pawn's CPlayer_ObserverServices. */
    void SetObserverMode(ObserverMode_t mode) const;

    /** Read `m_iszPlayerName` from the controller. Empty string if unavailable. */
    std::string GetPlayerName() const;

    /** Current model path of the pawn (scene node's CModelState). Empty string if unavailable. */
    std::string GetPawnModelName() const;

    /**
     * @brief Write `m_iszPlayerName` on the controller (128-byte fixed buffer).
     * Truncates to 127 chars + NUL. Replication to clients piggybacks on the
     * next state-change broadcast; pair with `ChangeTeam` or similar if you
     * need an immediate scoreboard refresh.
     */
    void SetPlayerName(const std::string& name) const;

    /**
     * @brief Apply transparency to the player pawn body. Weapons and wearables
     * are not affected (CS2 routes them through systems the server plugin
     * cannot reach). Pass `visible == true` to restore the default opaque state.
     *
     * @param visible  True to restore (mode=Normal, color=opaque white). False to hide.
     * @param alpha    Alpha byte applied when `visible == false`. Default 0 (fully invisible).
     */
    void SetVisible(bool visible, uint8_t alpha = 0) const;

    /**
     * @brief Teleport the pawn to an absolute origin/angles, optionally setting velocity.
     * Pass nullptr for any component to leave it unchanged. Calls CBaseEntity::Teleport
     * via the vtable offset registered as "Teleport" in gamedata.
     */
    void Teleport(const Vector* origin, const QAngle* angles, const Vector* velocity) const;

private:
    /** Resolve a schema field offset (delegates to the internal SchemaService). */
    // expectedSize > 0 validates the engine's field size on first lookup (warns on schema drift).
    int SchemaOffset(const char* className, const char* fieldName, int expectedSize = 0) const;

    EntitySystem* _entities;
    int _slot;
    CEntityInstance* _controller = nullptr;
};

}  // namespace VoltMod::Sdk
