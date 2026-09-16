#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
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

    const std::string classname(item);

    Schema::CPlayer_ItemServices services = ItemServices(pawn);
    if (!services)
        return false;

    if (!_bindings.GiveNamedItem)
        return false;

    if (_bindings.GiveNamedItem(services.Base(), classname.c_str()))
        return true;

    // Retry once with the pawn's team flipped for items restricted to the other team, then restore it.
    const auto team = static_cast<uint8_t>(pawn.Team());
    const auto other = static_cast<uint8_t>(team == TeamT ? TeamCT : (team == TeamCT ? TeamT : 0));
    if (other == 0)
        return false;

    pawn.SetTeam(other);
    bool given = _bindings.GiveNamedItem(services.Base(), classname.c_str()) != nullptr;
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

    _bindings.RemoveAllItems(services.Base(), removeSuit);
    return true;
}

}  // namespace VoltMod
