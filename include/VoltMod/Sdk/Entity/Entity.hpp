#pragma once

#include <VoltMod/Core/Slot.hpp>
#include <cstdint>
#include <string>

class CGameEntitySystem;
class CEntityInstance;
class CEntityIdentity;

namespace VoltMod::Sdk
{

/** @defgroup ButtonFlags Player Button Flags */
/** @{ */
constexpr uint64_t IN_ATTACK = 0x1;
constexpr uint64_t IN_JUMP = 0x2;
constexpr uint64_t IN_DUCK = 0x4;
constexpr uint64_t IN_FORWARD = 0x8;
constexpr uint64_t IN_BACK = 0x10;
constexpr uint64_t IN_USE = 0x20;
constexpr uint64_t IN_TURNLEFT = 0x80;
constexpr uint64_t IN_TURNRIGHT = 0x100;
constexpr uint64_t IN_MOVELEFT = 0x200;
constexpr uint64_t IN_MOVERIGHT = 0x400;
constexpr uint64_t IN_ATTACK2 = 0x800;
constexpr uint64_t IN_RELOAD = 0x2000;
constexpr uint64_t IN_SPEED = 0x10000;
constexpr uint64_t IN_SCORE = 0x200000000ULL;
constexpr uint64_t IN_ZOOM = 0x400000000ULL;
constexpr uint64_t IN_LOOK_AT_WEAPON = 0x800000000ULL;
/** @} */

/** @defgroup EntityFlags CBaseEntity::m_fFlags bit values (Flags_t in the CS2 schema) */
/** @{ */
constexpr uint32_t FL_ONGROUND = 1;
constexpr uint32_t FL_DUCKING = 2;
constexpr uint32_t FL_FROZEN = 32;
constexpr uint32_t FL_FAKECLIENT = 256;
constexpr uint32_t FL_GODMODE = 16384;
constexpr uint32_t FL_NOTARGET = 32768;
/** @} */

inline constexpr int MaxPlayers = Core::MaxPlayers;

/**
 * @brief Entity system access layer for the Source 2 engine.
 * Resolves CGameEntitySystem from IGameResourceService, provides player
 * controller lookup by slot, entity handle resolution, and button state reading.
 */
class EntitySystem
{
public:
    EntitySystem() = default;

    bool Initialize();
    CGameEntitySystem* GetEntitySystem();
    CEntityInstance* GetPlayerController(int slot);

    /**
     * Entity for a full EHandle (index + serial), or nullptr when the handle is
     * unset, stale, or its index was recycled by another entity. Validation happens
     * on the entity identity, so a handle that outlived its entity is always safe.
     */
    CEntityInstance* ResolveEntityHandle(uint32_t handle);

    /** Network entity index of @p entity, or -1 on null/unlinked. */
    int GetEntityIndex(CEntityInstance* entity) const;

    /** Raw EHandle (index + serial) of @p entity, or 0xFFFFFFFF (invalid) on null/unlinked. */
    uint32_t GetEntityHandle(CEntityInstance* entity) const;

    uint64_t GetPlayerButtons(int slot);

    /** The pawn's CPlayer_MovementServices* for @p slot, or nullptr (no pawn / offsets unresolved). */
    void* GetPlayerMovementServices(int slot);

    bool IsPlayerSlotValid(int slot);

    /** First entity of @p className after @p startAfter (nullptr = list head).
     *  nullptr when exhausted or the finder signature is unresolved. */
    CEntityInstance* FindByClassName(CEntityInstance* startAfter, const char* className);

    /** First entity whose targetname is @p name after @p startAfter (nullptr = list head).
     *  nullptr when exhausted or the finder signature is unresolved. */
    CEntityInstance* FindByName(CEntityInstance* startAfter, const char* name);

private:
    void ResolveSchemaOffsets();
    void ResolveFinderSignatures();
    CEntityIdentity* GetEntityIdentityByIndex(CGameEntitySystem* pSys, int index);

    /**
     * Read the CGameEntitySystem* out of IGameResourceService at the gamedata offset. nullptr if either is
     *unavailable.
     */
    CGameEntitySystem* ReadEntitySystemPointer();

    int _offsetPlayerPawn = -1;
    int _offsetMovementServices = -1;
    int _offsetButtons = -1;
    int _offsetButtonStates = -1;
    bool _schemaOffsetsResolved = false;
    void* _findByClassName = nullptr;
    void* _findByName = nullptr;
    bool _findersResolved = false;
};

}  // namespace VoltMod::Sdk
