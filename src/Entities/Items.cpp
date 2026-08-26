#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Entities/Items.hpp>
#include <VoltMod/Entities/PawnOps.hpp>
#include <cstdint>

namespace VoltMod
{

void* Items::ItemServices(const Pawn& pawn)
{
    static const LazyField services{"CBasePlayerPawn", "m_pItemServices", sizeof(void*)};

    if (!pawn || !services)
        return nullptr;

    return ReadAt<void*>(pawn.Raw(), services->Offset);
}

bool Items::Give(const Pawn& pawn, const char* item)
{
    if (!item || !*item)
        return false;

    auto* services = ItemServices(pawn);
    if (!services)
        return false;

    if (!_bindings.GiveNamedItem)
        return false;

    if (_bindings.GiveNamedItem.Call(services, item))
        return true;

    // A refusal is usually the weapon belonging to the other team's buy list. Retry once with
    // the pawn flipped, then put the team back before anything else can observe it.
    const auto team = static_cast<uint8_t>(pawn.Team);
    const auto other = static_cast<uint8_t>(team == TeamT ? TeamCT : (team == TeamCT ? TeamT : 0));
    if (other == 0)
        return false;

    pawn.Team = other;
    bool given = _bindings.GiveNamedItem.Call(services, item) != nullptr;
    pawn.Team = team;

    if (!given)
        Log::Warn("Items::Give: the engine refused '{}' for both teams.", item);
    return given;
}

bool Items::StripWeapons(const Pawn& pawn, bool removeSuit)
{
    auto* services = ItemServices(pawn);
    if (!services)
        return false;

    if (!_bindings.RemoveAllItems)
        return false;

    _bindings.RemoveAllItems.Call(services, removeSuit);
    return true;
}

}  // namespace VoltMod
