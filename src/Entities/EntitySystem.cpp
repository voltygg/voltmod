#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <entityhandle.h>
#include <string>
#include <string_view>
#include <vector>

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

static constexpr std::string_view ControllerClass = "cs_player_controller";

static_assert(MaxPlayers == ABSOLUTE_PLAYER_LIMIT);

EntitySystem::EntitySystem(VoltMod::Interfaces& interfaces, const VoltMod::Bindings& bindings)
    : _interfaces(interfaces), _bindings(bindings)
{}

EntitySystem::~EntitySystem()
{
    if (g_entitySystem == _interfaces.EntitySystem)
    {
        g_entitySystem = nullptr;
    }
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
    {
        return nullptr;
    }

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
    {
        return std::unexpected(Error::NotReady("IGameResourceService not available"));
    }

    if (!_bindings.GameEntitySystem)
    {
        return std::unexpected(Error::Unsupported("the GameEntitySystem offset did not bind"));
    }
    Log::Info("Gamedata loaded (entity system offset: {}).", _bindings.GameEntitySystem.Value());

    // Null is expected before the first map; OnServerStartup retries and callers decide whether it is required.
    Raw();
    return {};
}

void EntitySystem::OnServerStartup()
{
    // Each map creates a new system, so discard the old pointer before it is freed.
    SetEntitySystem(nullptr);

    if (Raw())
    {
        Log::Info("Entity system initialized.");
    }
    else
    {
        Log::Error("Entity system pointer could not be read from IGameResourceService.");
    }
}

CGameEntitySystem* EntitySystem::Raw()
{
    // The cache stays empty until the first server starts and is cleared for each new map.
    if (!_interfaces.EntitySystem)
    {
        SetEntitySystem(ReadEntitySystemPointer());
    }

    return _interfaces.EntitySystem;
}

Entity EntitySystem::Get(EntityRef ref)
{
    auto* system = Raw();
    if (!ref || !system)
    {
        return {};
    }

    // The identity's serial must match, since a destroyed entity leaves a dangling instance pointer.
    return {*this, system->GetEntityInstance(CEntityHandle(ref.Handle))};
}

CEntityInstance* EntitySystem::RawController(int slot)
{
    auto* system = Raw();
    if (!system || slot < 0 || slot >= MaxPlayers)
    {
        return nullptr;
    }

    // Controllers occupy indices 1..MaxPlayers; index 0 is worldspawn.
    CEntityIdentity* identity = system->GetEntityIdentity(CEntityIndex(slot + 1));
    if (!identity)
    {
        return nullptr;
    }

    // An index past the server's player limit holds an ordinary entity.
    if (ControllerClass != identity->GetClassname())
    {
        return nullptr;
    }
    return identity->m_pInstance;
}

VoltMod::Controller EntitySystem::Controller(int slot)
{
    return {*this, RawController(slot), slot};
}

VoltMod::Pawn EntitySystem::Pawn(int slot)
{
    return Controller(slot).Pawn();
}

VoltMod::Pawn EntitySystem::Pawn(EntityRef ref)
{
    return Get(ref).AsPawn();
}

Entity EntitySystem::Find(std::string_view className)
{
    if (!Raw() || className.empty())
    {
        return {};
    }

    // Walk the active list: the SDK's CEntityClass layout is stale, so its per-class chain reads garbage.
    const std::string name(className);
    EntityInstanceByClassIter_t iter(nullptr, name.c_str());
    return {*this, iter.Next()};
}

std::vector<Entity> EntitySystem::FindAll(std::string_view className)
{
    std::vector<Entity> found;
    if (!Raw() || className.empty())
    {
        return found;
    }

    const std::string name(className);
    EntityInstanceByClassIter_t iter(nullptr, name.c_str());
    for (CEntityInstance* entity = iter.Next(); entity; entity = iter.Next())
    {
        found.emplace_back(*this, entity);
    }
    return found;
}

std::vector<VoltMod::Pawn> EntitySystem::AlivePawns()
{
    std::vector<VoltMod::Pawn> pawns;
    pawns.reserve(MaxPlayers);
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (VoltMod::Pawn pawn = Pawn(slot); pawn.IsAlive())
        {
            pawns.push_back(pawn);
        }
    }
    return pawns;
}

}  // namespace VoltMod
