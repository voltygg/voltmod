#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <entity2/concreteentitylist.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>

/**
 * Published for ::GameEntitySystem() below, which the SDK calls with no context parameter to
 * reach a service through. Written and cleared by EntitySystem, so it never outlives a load cycle.
 */
static CGameEntitySystem* g_entitySystem = nullptr;

// The SDK's entity2 sources (entitykeyvalues.cpp) link against this accessor;
// route it to the framework's resolved entity system so both agree on the pointer.
CGameEntitySystem* GameEntitySystem()
{
    return g_entitySystem;
}

namespace VoltMod
{

EntitySystem::EntitySystem(Interfaces& interfaces, const Bindings& bindings)
    : _interfaces(interfaces), _bindings(bindings)
{}

EntitySystem::~EntitySystem()
{
    if (g_entitySystem == _interfaces.EntitySystem)
        g_entitySystem = nullptr;
}

void EntitySystem::SetEntitySystem(CGameEntitySystem* system)
{
    _interfaces.EntitySystem = system;
    g_entitySystem = system;  // publish for ::GameEntitySystem()
}

CGameEntitySystem* EntitySystem::ReadEntitySystemPointer()
{
    // Runs per call until the pointer resolves, so it stays a plain read.
    if (!_interfaces.GameResourceService)
        return nullptr;

    return _bindings.GameEntitySystem.Read(_interfaces.GameResourceService);
}

Status EntitySystem::Initialize()
{
    if (!_interfaces.GameResourceService)
        return std::unexpected(Error::NotReady("IGameResourceService not available"));

    if (!_bindings.GameEntitySystem)
        return std::unexpected(Error::Unsupported("the GameEntitySystem offset did not bind"));
    Log::Info("Gamedata loaded (entity system offset: {}).", _bindings.GameEntitySystem.Value());

    // The engine creates CGameEntitySystem during server startup, so a null read here is expected
    // on a cold load and OnServerStartup picks it up. Whether it resolved is load policy, not the
    // SDK's call: the caller reads GetEntitySystem() and decides.
    GetEntitySystem();
    return {};
}

void EntitySystem::OnServerStartup()
{
    // A new map gets a new CGameEntitySystem; keeping the old pointer would read freed memory.
    SetEntitySystem(nullptr);

    if (GetEntitySystem())
        Log::Info("Entity system initialized.");
    else
        Log::Error("Entity system pointer could not be read from IGameResourceService.");
}

CGameEntitySystem* EntitySystem::GetEntitySystem()
{
    // Null until the engine starts the first server, so an early load caches nothing and
    // re-reads on the next call. OnServerStartup drops the cache for each new map.
    if (!_interfaces.EntitySystem)
        SetEntitySystem(ReadEntitySystemPointer());

    return _interfaces.EntitySystem;
}

CEntityIdentity* EntitySystem::GetEntityIdentityByIndex(CGameEntitySystem* system, int index)
{
    if (!system || index < 0 || index >= MAX_TOTAL_ENTITIES)
        return nullptr;

    int chunk = index / MAX_ENTITIES_IN_LIST;
    int offset = index % MAX_ENTITIES_IN_LIST;

    CEntityIdentity* chunkBase = system->m_EntityList.m_pIdentityChunks[chunk];
    if (!chunkBase)
        return nullptr;

    return &chunkBase[offset];
}

Entity EntitySystem::Resolve(EntityRef ref)
{
    if (!ref)
        return {};

    int entryIndex = static_cast<int>(ref.Handle & 0x7FFF);  // low 15 bits index, high bits serial

    auto* system = GetEntitySystem();
    if (!system)
        return {};

    CEntityIdentity* identity = GetEntityIdentityByIndex(system, entryIndex);
    if (!identity)
        return {};

    // Validate index + serial on the identity itself (chunk memory, never freed) before
    // touching m_pInstance: once the entity is destroyed the identity slot is recycled and
    // m_pInstance dangles, so dereferencing the instance to validate is a use-after-free.
    if (static_cast<uint32_t>(identity->GetRefEHandle().ToInt()) != ref.Handle)
        return {};

    return {*this, identity->m_pInstance};
}

CEntityInstance* EntitySystem::RawController(int slot)
{
    auto* system = GetEntitySystem();
    if (!system || slot < 0 || slot >= MaxPlayers)
        return nullptr;

    // Controllers occupy entity indices 1..MaxPlayers (index 0 is worldspawn).
    CEntityIdentity* identity = GetEntityIdentityByIndex(system, slot + 1);
    if (!identity)
        return nullptr;

    return identity->m_pInstance;
}

VoltMod::Controller EntitySystem::Controller(int slot)
{
    return {*this, RawController(slot), slot};
}

Pawn EntitySystem::PawnOf(int slot)
{
    return Controller(slot).GetPawn();
}

int EntitySystem::SlotOf(const Pawn& pawn)
{
    static const LazyField controllerHandle{"CBasePlayerPawn", "m_hController", sizeof(uint32_t)};
    if (!pawn || !controllerHandle)
        return -1;

    Entity controller = Resolve(EntityRef{ReadAt<uint32_t>(pawn.Raw(), controllerHandle->Offset)});
    if (!controller)
        return -1;

    // Controllers occupy entity indices 1..MaxPlayers, the same mapping RawController uses.
    int slot = controller.Index() - 1;
    return IsValidSlot(slot) ? slot : -1;
}

uint64_t EntitySystem::Buttons(int slot)
{
    static const LazyField buttons{"CPlayer_MovementServices", "m_nButtons"};
    static const LazyField states{"CInButtonState", "m_pButtonStates"};

    auto* services = static_cast<uint8_t*>(MovementServices(slot));
    if (!services || !buttons || !states)
        return 0;

    // m_pButtonStates is uint64[3]: [0] held, [1] changed, [2] scroll.
    return MemberPtr<uint64_t>(services, buttons->Offset + states->Offset)[0];
}

void* EntitySystem::MovementServices(int slot)
{
    static const LazyField movement{"CBasePlayerPawn", "m_pMovementServices", sizeof(void*)};

    Pawn pawn = PawnOf(slot);
    if (!pawn || !movement)
        return nullptr;

    return ReadAt<uint8_t*>(pawn.Raw(), movement->Offset);
}

bool EntitySystem::IsPlayerSlotValid(int slot)
{
    return RawController(slot) != nullptr;
}

Entity EntitySystem::FindByClassName(const Entity& after, const char* className)
{
    auto* system = GetEntitySystem();
    if (!_bindings.FindEntityByClassName || !system || !className)
        return {};

    // The engine takes CEntitySystem*; the upcast happens here, where the complete types are in
    // scope, rather than inside the binding's own void* parameter.
    return {*this, _bindings.FindEntityByClassName(static_cast<CEntitySystem*>(system), after.Raw(), className)};
}

Entity EntitySystem::FindByName(const Entity& after, const char* name)
{
    auto* system = GetEntitySystem();
    if (!_bindings.FindEntityByName || !system || !name)
        return {};

    return {*this, _bindings.FindEntityByName(static_cast<CEntitySystem*>(system), after.Raw(), name, nullptr, nullptr,
                                              nullptr, nullptr)};
}

}  // namespace VoltMod
