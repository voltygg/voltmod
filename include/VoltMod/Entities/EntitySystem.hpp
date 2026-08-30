#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/Controller.hpp>
#include <VoltMod/Schema/Generated/CPlayer_MovementServices.hpp>
#include <cstdint>
#include <string_view>

namespace VoltMod
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

/**
 * @brief Entity lookup for the Source 2 engine, and the factory for every entity wrapper.
 *
 * Resolves CGameEntitySystem from IGameResourceService, then hands out @ref Entity, @ref Pawn and
 * @ref Controller values for slots, handles and classnames. Everything it returns is frame-local;
 * see @ref Entity for the validity contract.
 */
class EntitySystem
{
public:
    /** Both must outlive this service; the Runtime declares them above it. */
    EntitySystem(Interfaces& interfaces, const Bindings& bindings);
    ~EntitySystem();
    EntitySystem(const EntitySystem&) = delete;
    EntitySystem& operator=(const EntitySystem&) = delete;

    /**
     * @brief Bind the gamedata offset and attempt a first read of the entity system.
     * @return An error only when the service cannot work at all (no IGameResourceService, or the
     *         offset did not bind). Success with `GetEntitySystem() == nullptr` means the engine
     *         has not created CGameEntitySystem yet - expected before the first map load, and
     *         OnServerStartup picks it up. Whether that counts as degraded is the caller's load
     *         policy.
     */
    Status Initialize();

    /** Re-read the pointer for the new map. Called by the framework's StartupServer hook. */
    void OnServerStartup();

    CGameEntitySystem* GetEntitySystem();

    /** The controller in @p slot. Falsy when the slot is empty. */
    VoltMod::Controller Controller(int slot);

    /** The player pawn of @p slot (@ref Controller::GetPawn). Falsy when there is none. */
    Pawn PawnOf(int slot);

    /**
     * The entity @p ref points at, or a falsy wrapper when the ref is unset, stale, or its index
     * was recycled by another entity. Validation happens on the entity identity, so a ref that
     * outlived its entity is always safe to resolve.
     */
    Entity Resolve(EntityRef ref);

    /** First entity of @p className after @p after (a falsy Entity starts at the list head).
     *  Falsy when exhausted or the finder signature is unresolved. */
    Entity FindByClassName(const Entity& after, std::string_view className);

    /** First entity whose targetname is @p name after @p after (a falsy Entity starts at the list
     *  head). Falsy when exhausted or the finder signature is unresolved. */
    Entity FindByName(const Entity& after, std::string_view targetName);

    /** Slot owning @p pawn, or -1 when it is not a player pawn. Constant-time. */
    int SlotOf(const Pawn& pawn);

    /** Held buttons for @p slot (m_pButtonStates[0]), or 0. Read from @ref Controller::Possessed,
     *  so they arrive while dead or spectating too. */
    uint64_t Buttons(int slot);

    /** The player pawn's CPlayer_MovementServices for @p slot, or a falsy view when the slot
     *  has no pawn. */
    Schema::CPlayer_MovementServices MovementServices(int slot);

    bool IsPlayerSlotValid(int slot);

    /** @name Engine access for the wrappers.
     *  The wrappers are values with no services of their own, so their verbs reach the engine
     *  through the system that produced them. Public rather than `friend`: a value type that can
     *  reach the whole service graph is the locator shape this framework does not have, and these
     *  two are exactly the engine handles, not the graph. */
    /** @{ */
    [[nodiscard]] const Bindings& BindingsRef() const noexcept { return _bindings; }
    [[nodiscard]] const Interfaces& InterfacesRef() const noexcept { return _interfaces; }
    /** @} */

private:
    CEntityIdentity* GetEntityIdentityByIndex(CGameEntitySystem* system, int index);

    /** The raw controller entity for @p slot; the factories above wrap it. */
    CEntityInstance* RawController(int slot);

    /** Read the CGameEntitySystem* out of IGameResourceService at the gamedata offset. nullptr if
     *  either is unavailable. */
    CGameEntitySystem* ReadEntitySystemPointer();

    /** Sole writer of the pointer: keeps the ::GameEntitySystem() global in step with the member. */
    void SetEntitySystem(CGameEntitySystem* system);

    Interfaces& _interfaces;
    const Bindings& _bindings;
};

}  // namespace VoltMod
