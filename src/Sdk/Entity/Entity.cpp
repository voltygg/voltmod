#include "Sdk/Internal/Schema.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Sdk/Engine/GameData.hpp>
#include <VoltMod/Sdk/Engine/GameInterfaces.hpp>
#include <VoltMod/Sdk/Engine/MemoryAccess.hpp>
#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <bit>
#include <entity2/concreteentitylist.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>

namespace
{
/**
 * Published for ::GameEntitySystem() below, which the SDK calls with no context parameter to
 * reach a service through. Written and cleared by EntitySystem, so it never outlives a load cycle.
 */
CGameEntitySystem* g_entitySystem = nullptr;
}  // namespace

// The SDK's entity2 sources (entitykeyvalues.cpp) link against this accessor;
// route it to the framework's resolved entity system so both agree on the pointer.
CGameEntitySystem* GameEntitySystem()
{
    return g_entitySystem;
}

namespace VoltMod::Sdk
{
using namespace VoltMod::Core;

EntitySystem::EntitySystem(GameInterfaces& interfaces, GameData& gameData, SchemaService& schema)
    : _interfaces(interfaces), _gameData(gameData), _schema(schema)
{}

EntitySystem::~EntitySystem()
{
    if (g_entitySystem == _interfaces.EntitySystem)
        g_entitySystem = nullptr;
}

int EntitySystem::GetEntityIndex(CEntityInstance* entity) const
{
    return (entity && entity->m_pEntity) ? entity->m_pEntity->GetEntityIndex().Get() : -1;
}

uint32_t EntitySystem::GetEntityHandle(CEntityInstance* entity) const
{
    return (entity && entity->m_pEntity) ? static_cast<uint32_t>(entity->m_pEntity->m_EHandle.ToInt()) : 0xFFFFFFFFu;
}

void EntitySystem::ResolveSchemaOffsets()
{
    if (_schemaOffsetsResolved)
        return;

    auto& schema = _schema;

    _offsetPlayerPawn = schema.GetOffsetOf<uint32_t>("CBasePlayerController", "m_hPawn");
    _offsetPawnController = schema.GetOffsetOf<uint32_t>("CBasePlayerPawn", "m_hController");
    _offsetMovementServices = schema.GetOffsetOf<uint8_t*>("CBasePlayerPawn", "m_pMovementServices");
    _offsetButtons = schema.GetOffset("CPlayer_MovementServices", "m_nButtons");
    _offsetButtonStates = schema.GetOffset("CInButtonState", "m_pButtonStates");

    _schemaOffsetsResolved = true;
}

void EntitySystem::SetEntitySystem(CGameEntitySystem* system)
{
    _interfaces.EntitySystem = system;
    g_entitySystem = system;  // publish for ::GameEntitySystem()
}

CGameEntitySystem* EntitySystem::ReadEntitySystemPointer()
{
    // Runs per call until the pointer resolves, so it stays a plain read: Initialize caches the
    // offset rather than paying a gamedata lookup on paths that reach entities every frame.
    if (!_interfaces.GameResourceService || _offsetGameEntitySystem < 0)
        return nullptr;

    return ReadAt<CGameEntitySystem*>(_interfaces.GameResourceService, _offsetGameEntitySystem);
}

bool EntitySystem::Initialize()
{
    if (!_interfaces.GameResourceService)
    {
        Log::Error("IGameResourceService not available.");
        return false;
    }

    // "GameEntitySystem" = byte offset of the CGameEntitySystem* cached inside CGameResourceService.
    _offsetGameEntitySystem = _gameData.GetByteOffset("GameEntitySystem", MaxByteOffset, alignof(void*));
    if (_offsetGameEntitySystem < 0)
        return false;
    Log::Info("Gamedata loaded (entity system offset: {}).", _offsetGameEntitySystem);

    // The engine creates CGameEntitySystem during server startup, so a null read here is expected
    // on a cold load and OnServerStartup picks it up. Whether it resolved is load policy, not the
    // SDK's call: the caller reads IsResolved() and decides.
    GetEntitySystem();
    return true;
}

bool EntitySystem::IsResolved() const
{
    return _interfaces.EntitySystem != nullptr;
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

CEntityIdentity* EntitySystem::GetEntityIdentityByIndex(CGameEntitySystem* pSys, int index)
{
    if (!pSys || index < 0 || index >= MAX_TOTAL_ENTITIES)
        return nullptr;

    int chunk = index / MAX_ENTITIES_IN_LIST;
    int offset = index % MAX_ENTITIES_IN_LIST;

    CEntityIdentity* pChunk = pSys->m_EntityList.m_pIdentityChunks[chunk];
    if (!pChunk)
        return nullptr;

    return &pChunk[offset];
}

CEntityInstance* EntitySystem::ResolveEntityHandle(uint32_t handle)
{
    if (handle == 0xFFFFFFFF)  // INVALID_EHANDLE_INDEX: unset/cleared handle
        return nullptr;

    int entryIndex = handle & 0x7FFF;  // low 15 bits = entity index, high bits = serial number

    auto* pSys = GetEntitySystem();
    if (!pSys)
        return nullptr;

    CEntityIdentity* pIdentity = GetEntityIdentityByIndex(pSys, entryIndex);
    if (!pIdentity)
        return nullptr;

    // Validate index + serial on the identity itself (chunk memory, never freed) before
    // touching m_pInstance: once the entity is destroyed the identity slot is recycled and
    // m_pInstance dangles, so dereferencing the instance to validate is a use-after-free.
    if (static_cast<uint32_t>(pIdentity->GetRefEHandle().ToInt()) != handle)
        return nullptr;

    return pIdentity->m_pInstance;
}

CEntityInstance* EntitySystem::GetPlayerController(int slot)
{
    auto* pSys = GetEntitySystem();
    if (!pSys || slot < 0 || slot >= MaxPlayers)
        return nullptr;

    // Controllers occupy entity indices 1..MaxPlayers (index 0 is worldspawn).
    CEntityIdentity* pIdentity = GetEntityIdentityByIndex(pSys, slot + 1);
    if (!pIdentity)
        return nullptr;

    return pIdentity->m_pInstance;
}

int EntitySystem::SlotFromPawn(CEntityInstance* pawn)
{
    if (!pawn)
        return -1;

    if (!_schemaOffsetsResolved)
        ResolveSchemaOffsets();
    if (_offsetPawnController < 0)
        return -1;

    CEntityInstance* controller = ResolveEntityHandle(ReadAt<uint32_t>(pawn, _offsetPawnController));
    if (!controller)
        return -1;

    // Controllers occupy entity indices 1..MaxPlayers, the same mapping GetPlayerController uses.
    int slot = GetEntityIndex(controller) - 1;
    return IsValidSlot(slot) ? slot : -1;
}

uint64_t EntitySystem::GetPlayerButtons(int slot)
{
    auto* pMovementServices = static_cast<uint8_t*>(GetPlayerMovementServices(slot));
    if (!pMovementServices || _offsetButtons < 0 || _offsetButtonStates < 0)
        return 0;

    auto* pButtonStates = MemberPtr<uint64_t>(pMovementServices, _offsetButtons + _offsetButtonStates);

    return pButtonStates[0];  // m_pButtonStates is uint64[3]: [0] held, [1] changed, [2] scroll
}

void* EntitySystem::GetPlayerMovementServices(int slot)
{
    if (!_schemaOffsetsResolved)
        ResolveSchemaOffsets();

    if (_offsetPlayerPawn < 0 || _offsetMovementServices < 0)
        return nullptr;

    CEntityInstance* pController = GetPlayerController(slot);
    if (!pController)
        return nullptr;

    uint32_t hPawn = ReadAt<uint32_t>(pController, _offsetPlayerPawn);
    CEntityInstance* pPawn = ResolveEntityHandle(hPawn);
    if (!pPawn)
        return nullptr;

    return ReadAt<uint8_t*>(pPawn, _offsetMovementServices);
}

bool EntitySystem::IsPlayerSlotValid(int slot)
{
    return GetPlayerController(slot) != nullptr;
}

namespace
{
// Prototypes mirror CS2Fixes' src/addresses.h; re-verify there after CS2 updates.
using FindByClassNameFn = CEntityInstance* (*)(CEntitySystem * system, CEntityInstance* startAfter,
                                               const char* className);
using FindByNameFn = CEntityInstance* (*)(CEntitySystem * system, CEntityInstance* startAfter, const char* name,
                                          CEntityInstance* searching, CEntityInstance* activator,
                                          CEntityInstance* caller, void* filter);
}  // namespace

void EntitySystem::ResolveFinderSignatures()
{
    if (_findersResolved)
        return;

    _findByClassName = _gameData.FindSignature("CGameEntitySystem_FindEntityByClassName");
    _findByName = _gameData.FindSignature("CGameEntitySystem_FindEntityByName");

    if (!_findByClassName || !_findByName)
        Log::Warn("Entity finder signature(s) not resolved; FindByClassName/FindByName are disabled.");

    _findersResolved = true;
}

CEntityInstance* EntitySystem::FindByClassName(CEntityInstance* startAfter, const char* className)
{
    ResolveFinderSignatures();

    auto* pSys = GetEntitySystem();
    if (!_findByClassName || !pSys || !className)
        return nullptr;

    return std::bit_cast<FindByClassNameFn>(_findByClassName)(pSys, startAfter, className);
}

CEntityInstance* EntitySystem::FindByName(CEntityInstance* startAfter, const char* name)
{
    ResolveFinderSignatures();

    auto* pSys = GetEntitySystem();
    if (!_findByName || !pSys || !name)
        return nullptr;

    return std::bit_cast<FindByNameFn>(_findByName)(pSys, startAfter, name, nullptr, nullptr, nullptr, nullptr);
}

}  // namespace VoltMod::Sdk
