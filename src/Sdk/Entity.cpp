#include "Sdk/Schema.hpp"

#include <CS2Kit/Core/Log.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/Entity.hpp>
#include <CS2Kit/Sdk/GameData.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <CS2Kit/Sdk/MemoryAccess.hpp>
#include <bit>
#include <entity2/concreteentitylist.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>

// The SDK's entity2 sources (entitykeyvalues.cpp) link against this accessor;
// route it to the kit's resolved entity system so both agree on the pointer.
CGameEntitySystem* GameEntitySystem()
{
    auto* services = CS2Kit::Detail::RtOrNull();
    return services ? services->Entities.GetEntitySystem() : nullptr;
}

namespace CS2Kit::Sdk
{
using namespace CS2Kit::Core;

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

    auto& schema = CS2Kit::Detail::Rt().Schema();

    _offsetPlayerPawn = schema.GetOffsetOf<uint32_t>("CBasePlayerController", "m_hPawn");
    _offsetMovementServices = schema.GetOffsetOf<uint8_t*>("CBasePlayerPawn", "m_pMovementServices");
    _offsetButtons = schema.GetOffset("CPlayer_MovementServices", "m_nButtons");
    _offsetButtonStates = schema.GetOffset("CInButtonState", "m_pButtonStates");

    _schemaOffsetsResolved = true;
}

CGameEntitySystem* EntitySystem::ReadEntitySystemPointer()
{
    auto& interfaces = CS2Kit::Detail::Rt().Interfaces;
    if (!interfaces.GameResourceService)
        return nullptr;

    // "GameEntitySystem" = byte offset of the CGameEntitySystem* cached inside CGameResourceService.
    int offsetGameEntitySystem = CS2Kit::Detail::Rt().GameData.GetOffset("GameEntitySystem");
    if (offsetGameEntitySystem < 0)
        return nullptr;

    return ReadAt<CGameEntitySystem*>(interfaces.GameResourceService, offsetGameEntitySystem);
}

bool EntitySystem::Initialize()
{
    auto& interfaces = CS2Kit::Detail::Rt().Interfaces;

    if (!interfaces.GameResourceService)
    {
        Log::Error("IGameResourceService not available.");
        return false;
    }

    int offsetGameEntitySystem = CS2Kit::Detail::Rt().GameData.GetOffset("GameEntitySystem");

    if (offsetGameEntitySystem < 0)
    {
        Log::Error("GameEntitySystem offset not found in gamedata.");
        return false;
    }
    Log::Info("Gamedata loaded (entity system offset: {}).", offsetGameEntitySystem);

    interfaces.EntitySystem = ReadEntitySystemPointer();

    // Nothing that touches an entity works without this, so reporting success here just moved the
    // failure to the first confusing symptom instead of the load report.
    if (!interfaces.EntitySystem)
    {
        Log::Error("Entity system pointer could not be read from IGameResourceService.");
        return false;
    }

    Log::Info("Entity system initialized.");
    return true;
}

CGameEntitySystem* EntitySystem::GetEntitySystem()
{
    auto& interfaces = CS2Kit::Detail::Rt().Interfaces;

    if (!interfaces.EntitySystem)
        interfaces.EntitySystem = ReadEntitySystemPointer();

    return interfaces.EntitySystem;
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

    auto& gameData = CS2Kit::Detail::Rt().GameData;
    _findByClassName = gameData.FindSignature("CGameEntitySystem_FindEntityByClassName");
    _findByName = gameData.FindSignature("CGameEntitySystem_FindEntityByName");

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

}  // namespace CS2Kit::Sdk
