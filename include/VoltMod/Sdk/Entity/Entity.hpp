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

/** Sentinel EHandle value for an unset/cleared handle (INVALID_EHANDLE_INDEX). */
inline constexpr uint32_t InvalidEntityHandle = 0xFFFFFFFFu;

class GameData;
struct GameInterfaces;
class PlayerController;
class SchemaService;  // Internal type (src/Sdk/Internal/Schema.hpp), kept out of the public graph.

/**
 * @brief Entity system access layer for the Source 2 engine.
 * Resolves CGameEntitySystem from IGameResourceService, provides player
 * controller lookup by slot, entity handle resolution, and button state reading.
 */
class EntitySystem
{
public:
    /** All three must outlive this service; the Runtime declares them above it. */
    EntitySystem(GameInterfaces& interfaces, GameData& gameData, SchemaService& schema);
    ~EntitySystem();
    EntitySystem(const EntitySystem&) = delete;
    EntitySystem& operator=(const EntitySystem&) = delete;

    /**
     * @brief Resolve the gamedata offset and attempt a first read of the entity system.
     * @return False only when the service cannot work at all (no IGameResourceService, or the
     *         offset is missing). True with IsResolved() false means the engine has not created
     *         CGameEntitySystem yet - expected before the first map load, and OnServerStartup
     *         picks it up. Whether that counts as degraded is the caller's load policy.
     */
    bool Initialize();

    /** True once the CGameEntitySystem pointer has been read for the current map. */
    bool IsResolved() const;

    /** Re-read the pointer for the new map. Called by the framework's StartupServer hook. */
    void OnServerStartup();

    CGameEntitySystem* GetEntitySystem();
    CEntityInstance* GetPlayerController(int slot);

    /**
     * Typed wrapper around the controller in @p slot, built from this system and its schema.
     * The result is a transient value - resolve it again rather than storing it across frames.
     * Defined in PlayerController.cpp; include `<VoltMod/Sdk/Entity/PlayerController.hpp>` to call it.
     */
    PlayerController Controller(int slot);

    /**
     * Entity for a full EHandle (index + serial), or nullptr when the handle is
     * unset, stale, or its index was recycled by another entity. Validation happens
     * on the entity identity, so a handle that outlived its entity is always safe.
     */
    CEntityInstance* ResolveEntityHandle(uint32_t handle);

    /** Network entity index of @p entity, or -1 on null/unlinked. */
    int GetEntityIndex(CEntityInstance* entity) const;

    /**
     * Slot owning @p pawn, or -1 when it is not a player pawn.
     *
     * Constant-time: reads the pawn's own `m_hController` back-reference rather than scanning
     * the slots. Callers on per-damage or per-tick paths depend on that, so keep it O(1).
     */
    int SlotFromPawn(CEntityInstance* pawn);

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
    // PlayerController stays a three-pointer transient by reaching the siblings through these
    // rather than carrying copies. Private, not @internal: a service graph reachable from a value
    // type is the locator shape this framework does not have. `Ref` because `GameData` is a type.
    friend class PlayerController;
    SchemaService& Schema() { return _schema; }
    const GameInterfaces& Interfaces() const { return _interfaces; }
    const GameData& GameDataRef() const { return _gameData; }

    void ResolveSchemaOffsets();
    void ResolveFinderSignatures();
    CEntityIdentity* GetEntityIdentityByIndex(CGameEntitySystem* pSys, int index);

    /**
     * Read the CGameEntitySystem* out of IGameResourceService at the gamedata offset. nullptr if either is
     *unavailable.
     */
    CGameEntitySystem* ReadEntitySystemPointer();

    /** Sole writer of the pointer: keeps the ::GameEntitySystem() global in step with the member. */
    void SetEntitySystem(CGameEntitySystem* system);

    GameInterfaces& _interfaces;
    GameData& _gameData;
    SchemaService& _schema;
    int _offsetGameEntitySystem = -1;
    int _offsetPlayerPawn = -1;
    int _offsetPawnController = -1;
    int _offsetMovementServices = -1;
    int _offsetButtons = -1;
    int _offsetButtonStates = -1;
    bool _schemaOffsetsResolved = false;
    void* _findByClassName = nullptr;
    void* _findByName = nullptr;
    bool _findersResolved = false;
};

}  // namespace VoltMod::Sdk
