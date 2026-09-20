#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <entity2/concreteentitylist.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <string>

/**
 * The SDK calls ::GameEntitySystem() without context. EntitySystem owns this pointer and clears it
 * before the load cycle ends.
 */
static CGameEntitySystem* g_entitySystem = nullptr;

// entity2 sources link against this accessor, so return the framework's resolved system.
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
    // Retry until the pointer resolves; keep this path a plain read.
    if (!_interfaces.GameResourceService || _isRealSystem == false)
        return nullptr;

    CGameEntitySystem* system = _bindings.GameEntitySystem.Read(_interfaces.GameResourceService);
    // Check the vtable before using a pointer from a drifted offset.
    if (system && !_isRealSystem)
    {
        const void* table = FindVirtualTable("server", "CGameEntitySystem");
        _isRealSystem = !table || IsInstanceOf(system, table);
        if (!*_isRealSystem)
        {
            Log::Error("The GameEntitySystem offset {} does not reach a CGameEntitySystem; entity lookups are off.",
                       _bindings.GameEntitySystem.Value());
            return nullptr;
        }
    }
    return system;
}

Status EntitySystem::Initialize()
{
    if (!_interfaces.GameResourceService)
        return std::unexpected(Error::NotReady("IGameResourceService not available"));

    if (!_bindings.GameEntitySystem)
        return std::unexpected(Error::Unsupported("the GameEntitySystem offset did not bind"));
    Log::Info("Gamedata loaded (entity system offset: {}).", _bindings.GameEntitySystem.Value());

    // Null is expected before the first map; OnServerStartup retries and callers decide whether it is required.
    GetEntitySystem();
    return {};
}

void EntitySystem::OnServerStartup()
{
    // Each map creates a new system, so discard the old pointer before it is freed.
    SetEntitySystem(nullptr);

    if (GetEntitySystem())
        Log::Info("Entity system initialized.");
    else
        Log::Error("Entity system pointer could not be read from IGameResourceService.");
}

CGameEntitySystem* EntitySystem::GetEntitySystem()
{
    // The cache stays empty until the first server starts and is cleared for each new map.
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

    // A freed identity keeps a dangling m_pInstance; only its handle says the entry is dead.
    CEntityIdentity* identity = &chunkBase[offset];
    if (identity->GetRefEHandle().GetEntryIndex() != index)
        return nullptr;
    return identity;
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

    // Validate the identity before m_pInstance because a destroyed entity leaves a dangling pointer.
    if (static_cast<uint32_t>(identity->GetRefEHandle().ToInt()) != ref.Handle)
        return {};

    return {*this, identity->m_pInstance};
}

CEntityInstance* EntitySystem::RawController(int slot)
{
    auto* system = GetEntitySystem();
    if (!system || slot < 0 || slot >= MaxPlayers)
        return nullptr;

    // Controllers occupy indices 1..MaxPlayers; index 0 is worldspawn.
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
    Entity controller = Resolve(EntityRef{pawn.ControllerHandle()});
    if (!controller)
        return -1;

    // Keep controller indices consistent with RawController.
    int slot = controller.Index() - 1;
    return IsValidSlot(slot) ? slot : -1;
}

uint64_t EntitySystem::Buttons(int slot)
{
    // m_pButtonStates is uint64[3]; read it from the possessed pawn because the observer owns input while dead.
    const Schema::CPlayer_MovementServices services = Controller(slot).Possessed().MovementServices();
    return services ? services.Buttons().ButtonStates(0) : 0;
}

Schema::CPlayer_MovementServices EntitySystem::MovementServices(int slot)
{
    return PawnOf(slot).MovementServices();
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

    // Upcast where complete types are visible; the engine uses the temporary name only during lookup.
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
