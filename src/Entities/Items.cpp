#include "Entities/Schema.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Entities/Items.hpp>
#include <VoltMod/Entities/PawnOps.hpp>
#include <VoltMod/Entities/PlayerController.hpp>
#include <cstdint>

namespace VoltMod
{

Items::Items(const Bindings& bindings, SchemaService& schema) : _bindings(bindings), _schema(schema) {}

void* Items::ItemServices(const PlayerController& pc) const
{
    auto* pawn = pc.GetPawn();
    if (!pawn)
        return nullptr;

    int offset = _schema.GetOffsetOf<void*>("CBasePlayerPawn", "m_pItemServices");
    if (offset < 0)
        return nullptr;

    return ReadAt<void*>(pawn, offset);
}

bool Items::Give(const PlayerController& pc, const char* item)
{
    if (!item || !*item)
        return false;

    auto* services = ItemServices(pc);
    if (!services)
        return false;

    if (!_bindings.GiveNamedItem)
        return false;

    if (_bindings.GiveNamedItem.Call(services, item))
        return true;

    // A refusal is usually the weapon belonging to the other team's buy list. Retry once with
    // the pawn flipped, then put the team back before anything else can observe it.
    int team = pc.GetTeam();
    int other = team == TeamT ? TeamCT : (team == TeamCT ? TeamT : 0);
    if (other == 0)
        return false;

    pc.SetPawnField<uint8_t>("CBaseEntity", "m_iTeamNum", static_cast<uint8_t>(other));
    bool given = _bindings.GiveNamedItem.Call(services, item) != nullptr;
    pc.SetPawnField<uint8_t>("CBaseEntity", "m_iTeamNum", static_cast<uint8_t>(team));

    if (!given)
        Log::Warn("Items::Give: the engine refused '{}' for both teams.", item);
    return given;
}

bool Items::StripWeapons(const PlayerController& pc, bool removeSuit)
{
    auto* services = ItemServices(pc);
    if (!services)
        return false;

    if (!_bindings.RemoveAllItems)
        return false;

    _bindings.RemoveAllItems.Call(services, removeSuit);
    return true;
}

}  // namespace VoltMod
