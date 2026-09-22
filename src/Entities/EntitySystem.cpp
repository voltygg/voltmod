#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <entityhandle.h>
#include <string>
#include <string_view>
#include <tier1/utlvector.h>
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

EntitySystem::EntitySystem(Interfaces& interfaces, const Bindings& bindings)
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
    GetEntitySystem();
    return {};
}

void EntitySystem::OnServerStartup()
{
    // Each map creates a new system, so discard the old pointer before it is freed.
    SetEntitySystem(nullptr);

    if (GetEntitySystem())
    {
        Log::Info("Entity system initialized.");
    }
    else
    {
        Log::Error("Entity system pointer could not be read from IGameResourceService.");
    }
}

CGameEntitySystem* EntitySystem::GetEntitySystem()
{
    // The cache stays empty until the first server starts and is cleared for each new map.
    if (!_interfaces.EntitySystem)
    {
        SetEntitySystem(ReadEntitySystemPointer());
    }

    return _interfaces.EntitySystem;
}

Entity EntitySystem::Resolve(EntityRef ref)
{
    auto* system = GetEntitySystem();
    if (!ref || !system)
    {
        return {};
    }

    // The identity's serial must match, since a destroyed entity leaves a dangling instance pointer.
    return {*this, system->GetEntityInstance(CEntityHandle(ref.Handle))};
}

CEntityInstance* EntitySystem::RawController(int slot)
{
    auto* system = GetEntitySystem();
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

Pawn EntitySystem::PawnOf(int slot)
{
    return Controller(slot).GetPawn();
}

int EntitySystem::SlotOf(const Pawn& pawn)
{
    Entity controller = Resolve(EntityRef{pawn.ControllerHandle()});
    if (!controller)
    {
        return -1;
    }

    // Keep controller indices consistent with RawController.
    int slot = controller.Index() - 1;
    return IsValidSlot(slot) ? slot : -1;
}

int EntitySystem::PlayerSlotOf(const Entity& entity)
{
    // SlotOf reads a pawn-only field, so every other class is turned away first.
    if (!entity || entity.ClassName() != "player")
    {
        return -1;
    }
    return SlotOf(Pawn{*this, entity.Raw()});
}

std::vector<Entity> EntitySystem::WeaponsOf(const Pawn& pawn)
{
    std::vector<Entity> weapons;
    if (!pawn)
    {
        return weapons;
    }
    const Schema::CPlayer_WeaponServices services = pawn.WeaponServices();
    if (!services)
    {
        return weapons;
    }
    // The schema's CNetworkUtlVectorBase<CHandle<T>> is laid out as a CUtlVector.
    const auto* handles = static_cast<const CUtlVector<CEntityHandle>*>(services.MyWeapons());
    if (!handles)
    {
        return weapons;
    }
    for (int i = 0; i < handles->Count(); ++i)
    {
        const Entity weapon = Resolve(EntityRef{static_cast<uint32_t>(handles->Element(i).ToInt())});
        if (weapon)
        {
            weapons.push_back(weapon);
        }
    }
    return weapons;
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
    if (!GetEntitySystem() || className.empty())
    {
        return {};
    }

    // An exact match continues along the engine's per-class chain instead of the whole active list.
    CEntityIdentity* start = after ? after.Raw()->m_pEntity : nullptr;
    if (start && start->m_pClass && className.find('*') == std::string_view::npos && after.ClassName() == className)
    {
        for (CEntityIdentity* next = start->m_pNextByClass; next; next = next->m_pNextByClass)
        {
            if ((next->m_flags & EF_MARKED_FOR_DELETE) == 0)
            {
                return {*this, next->m_pInstance};
            }
        }
        return {};
    }

    const std::string name(className);
    if (!after)
    {
        EntityInstanceByClassIter_t iter(name.c_str());
        return {*this, iter.First()};
    }
    // Wildcards and unresolved classes walk the active list from `after`.
    EntityInstanceByClassIter_t iter(after.Raw(), name.c_str());
    return {*this, iter.Next()};
}

Entity EntitySystem::FindByName(const Entity& after, std::string_view targetName)
{
    if (!GetEntitySystem() || targetName.empty())
    {
        return {};
    }

    // The name iterator cannot start mid-list, so step past `after` first.
    const std::string name(targetName);
    EntityInstanceByNameIter_t iter(name.c_str());
    CEntityInstance* entity = iter.First();
    if (after)
    {
        while (entity && entity != after.Raw())
        {
            entity = iter.Next();
        }
        if (entity)
        {
            entity = iter.Next();
        }
    }
    return {*this, entity};
}

}  // namespace VoltMod
