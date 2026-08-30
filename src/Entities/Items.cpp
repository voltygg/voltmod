#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Entities/Items.hpp>
#include <VoltMod/Entities/PawnOps.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <cstdint>
#include <string>

namespace VoltMod
{

Schema::CPlayer_ItemServices Items::ItemServices(const Pawn& pawn)
{
    return pawn.ItemServices();
}

bool Items::Give(const Pawn& pawn, std::string_view item)
{
    if (item.empty())
        return false;

    // GiveNamedItem looks the classname up during the call; the copy only has to outlive it.
    const std::string classname(item);

    Schema::CPlayer_ItemServices services = ItemServices(pawn);
    if (!services)
        return false;

    if (!_bindings.GiveNamedItem)
        return false;

    if (_bindings.GiveNamedItem.Call(services.Base(), classname.c_str()))
        return true;

    // A refusal is usually the weapon belonging to the other team's buy list. Retry once with
    // the pawn flipped, then put the team back before anything else can observe it.
    const auto team = static_cast<uint8_t>(pawn.Team());
    const auto other = static_cast<uint8_t>(team == TeamT ? TeamCT : (team == TeamCT ? TeamT : 0));
    if (other == 0)
        return false;

    pawn.SetTeam(other);
    bool given = _bindings.GiveNamedItem.Call(services.Base(), classname.c_str()) != nullptr;
    pawn.SetTeam(team);

    if (!given)
        Log::Warn("Items::Give: the engine refused '{}' for both teams.", item);
    return given;
}

bool Items::StripWeapons(const Pawn& pawn, bool removeSuit)
{
    Schema::CPlayer_ItemServices services = ItemServices(pawn);
    if (!services)
        return false;

    if (!_bindings.RemoveAllItems)
        return false;

    _bindings.RemoveAllItems.Call(services.Base(), removeSuit);
    return true;
}

}  // namespace VoltMod
