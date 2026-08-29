#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <entity2/concreteentitylist.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <string>

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
    static const SchemaField<uint32_t> controllerHandle{"CBasePlayerPawn", "m_hController"};

    const uint32_t handle = SchemaPtr{pawn.Raw()}.Get(controllerHandle, InvalidEntityHandle);
    Entity controller = Resolve(EntityRef{handle});
    if (!controller)
        return -1;

    // Controllers occupy entity indices 1..MaxPlayers, the same mapping RawController uses.
    int slot = controller.Index() - 1;
    return IsValidSlot(slot) ? slot : -1;
}

// The movement services live on a sub-object the pawn points at, so reaching them is a follow
// rather than a fixed offset. Which pawn to start from is the caller's choice.
static SchemaPtr MovementServicesOf(CEntityInstance* pawn)
{
    static const SchemaField<void*> movement{"CBasePlayerPawn", "m_pMovementServices"};

    return SchemaPtr{pawn}.Follow(movement);
}

uint64_t EntitySystem::Buttons(int slot)
{
    // m_nButtons is an embedded CInButtonState; m_pButtonStates inside it is uint64[3]:
    // [0] held, [1] changed, [2] scroll.
    static const SchemaField<void> buttons{"CPlayer_MovementServices", "m_nButtons"};
    static const SchemaField<uint64_t[]> states{"CInButtonState", "m_pButtonStates"};

    // The possessed pawn, not MovementServices(): while dead the observer pawn takes the input.
    const uint64_t* held = MovementServicesOf(Controller(slot).Possessed().Raw()).At(buttons).Ptr(states);
    return held ? held[0] : 0;
}

SchemaPtr EntitySystem::MovementServices(int slot)
{
    return MovementServicesOf(PawnOf(slot).Raw());
}

bool EntitySystem::IsPlayerSlotValid(int slot)
{
    return RawController(slot) != nullptr;
}

Entity EntitySystem::FindByClassName(const Entity& after, std::string_view className)
{
    auto* system = GetEntitySystem();
    if (!_bindings.FindEntityByClassName || !system || className.empty())
        return {};

    // The engine takes CEntitySystem*; the upcast happens here, where the complete types are in
    // scope, rather than inside the binding's own void* parameter. It compares the name during the
    // walk and does not keep it, so a NUL-terminated temporary is enough.
    const std::string name(className);
    return {*this, _bindings.FindEntityByClassName(static_cast<CEntitySystem*>(system), after.Raw(), name.c_str())};
}

Entity EntitySystem::FindByName(const Entity& after, std::string_view targetName)
{
    auto* system = GetEntitySystem();
    if (!_bindings.FindEntityByName || !system || targetName.empty())
        return {};

    const std::string name(targetName);
    return {*this, _bindings.FindEntityByName(static_cast<CEntitySystem*>(system), after.Raw(), name.c_str(), nullptr,
                                              nullptr, nullptr, nullptr)};
}

}  // namespace VoltMod
