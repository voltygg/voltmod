#include "../Internal/Schema.hpp"
#include "../Internal/VirtualCall.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Sdk/Engine/GameData.hpp>
#include <VoltMod/Sdk/Engine/MemoryAccess.hpp>
#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <VoltMod/Sdk/Entity/ItemService.hpp>
#include <VoltMod/Sdk/Entity/PawnOps.hpp>
#include <VoltMod/Sdk/Entity/PlayerController.hpp>
#include <cstdint>

namespace VoltMod::Sdk
{

using namespace VoltMod::Core;

namespace
{

// CNetworkUtlVectorBase<CHandle<T>>: element count at +0, element pointer at +8. Read only.
struct HandleVectorView
{
    int32_t Count;
    int32_t _pad;
    const uint32_t* Elements;
};

// Twelve weapon slots is already generous; the cap only bounds a corrupt count.
constexpr int MaxWeapons = 24;

}  // namespace

ItemService::ItemService(EntitySystem& entities, GameData& gameData, SchemaService& schema)
    : _entities(entities), _gameData(gameData), _schema(schema)
{}

void* ItemService::ItemServices(const PlayerController& pc) const
{
    auto* pawn = pc.GetPawn();
    if (!pawn)
        return nullptr;

    int offset = _schema.GetOffsetOf<void*>("CBasePlayerPawn", "m_pItemServices");
    if (offset < 0)
        return nullptr;

    return ReadAt<void*>(pawn, offset);
}

bool ItemService::Give(const PlayerController& pc, const char* item)
{
    if (!item || !*item)
        return false;

    auto* services = ItemServices(pc);
    if (!services)
        return false;

    int index = _gameData.GetVtableIndex("GiveNamedItem");
    if (index < 0)
        return false;

    if (CallVirtual<void*>(index, services, item))
        return true;

    // A refusal is usually the weapon belonging to the other team's buy list. Retry once with
    // the pawn flipped, then put the team back before anything else can observe it.
    int team = pc.GetTeam();
    int other = team == TeamT ? TeamCT : (team == TeamCT ? TeamT : 0);
    if (other == 0)
        return false;

    pc.SetPawnField<uint8_t>("CBaseEntity", "m_iTeamNum", static_cast<uint8_t>(other));
    bool given = CallVirtual<void*>(index, services, item) != nullptr;
    pc.SetPawnField<uint8_t>("CBaseEntity", "m_iTeamNum", static_cast<uint8_t>(team));

    if (!given)
        Log::Warn("ItemService::Give: the engine refused '{}' for both teams.", item);
    return given;
}

bool ItemService::StripWeapons(const PlayerController& pc, bool removeSuit)
{
    auto* services = ItemServices(pc);
    if (!services)
        return false;

    int index = _gameData.GetVtableIndex("RemoveAllItems");
    if (index < 0)
        return false;

    CallVirtual<void>(index, services, removeSuit);
    return true;
}

std::vector<CEntityInstance*> ItemService::GetWeapons(const PlayerController& pc) const
{
    std::vector<CEntityInstance*> weapons;

    auto* pawn = pc.GetPawn();
    if (!pawn)
        return weapons;

    int servicesOffset = _schema.GetOffsetOf<void*>("CBasePlayerPawn", "m_pWeaponServices");
    if (servicesOffset < 0)
        return weapons;

    auto* weaponServices = ReadAt<void*>(pawn, servicesOffset);
    if (!weaponServices)
        return weapons;

    int listOffset = _schema.GetOffset("CPlayer_WeaponServices", "m_hMyWeapons");
    if (listOffset < 0)
        return weapons;

    const auto* view = MemberPtr<const HandleVectorView>(weaponServices, listOffset);
    if (!view->Elements)
        return weapons;

    for (int32_t i = 0; i < view->Count && i < MaxWeapons; ++i)
    {
        if (auto* weapon = _entities.ResolveEntityHandle(view->Elements[i]))
            weapons.push_back(weapon);
    }
    return weapons;
}

}  // namespace VoltMod::Sdk
