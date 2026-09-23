#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <eiface.h>
#include <string>
#include <utility>

namespace VoltMod
{

// Passed by value across the plugin boundary, so the layout is ABI.
static_assert(sizeof(Controller) <= 104, "Controller is a frame-local value; keep the field list tight.");

Controller::Controller(EntitySystem& entities, CEntityInstance* raw, int slot) : Entity(entities, raw), _slot(slot)
{
    _pawn = entities.Resolve(PlayerPawnRef()).Raw();
}

Pawn Controller::GetPawn() const
{
    return _sys ? Pawn{*_sys, _pawn} : Pawn{};
}

Pawn Controller::Possessed() const
{
    if (!_sys)
    {
        return {};
    }
    return Pawn{*_sys, _sys->Resolve(PawnRef()).Raw()};
}

int Controller::Money() const
{
    const Schema::CCSPlayerController_InGameMoneyServices money = InGameMoneyServices();
    return money ? money.Account() : 0;
}

Status Controller::SetMoney(int amount) const
{
    const Schema::CCSPlayerController_InGameMoneyServices money = InGameMoneyServices();
    if (!money)
    {
        return std::unexpected(Error::NotReady("money services unavailable"));
    }

    money.SetAccount(amount);
    return {};
}

Status Controller::Kick(std::string_view reason) const
{
    if (!_e || !_sys)
    {
        return std::unexpected(Error::NotReady("no controller"));
    }

    auto* engine = _sys->InterfacesRef().Engine;
    if (!engine)
    {
        return std::unexpected(Error::NotReady("IVEngineServer2 not available"));
    }

    const std::string text(reason);
    engine->DisconnectClient(CPlayerSlot(_slot), NETWORK_DISCONNECT_KICKED, text.c_str());
    return {};
}

Status Controller::ChangeTeam(VoltMod::Team team) const
{
    if (!_e || !_sys)
    {
        return std::unexpected(Error::NotReady("no controller"));
    }
    if (team != Team::Spectator && !IsPlaying(team))
    {
        return std::unexpected(Error::Invalid("a player can only join the spectators, T or CT"));
    }

    const auto& changeTeam = _sys->BindingsRef().ChangeTeam;
    if (!changeTeam)
    {
        return std::unexpected(Error::Unsupported("the 'CCSPlayerController::ChangeTeam' vtable slot did not bind"));
    }

    changeTeam(_e, std::to_underlying(team));
    return {};
}

Status Controller::Respawn() const
{
    if (!_e || !_sys)
    {
        return std::unexpected(Error::NotReady("no controller"));
    }

    const auto& respawn = _sys->BindingsRef().Respawn;
    if (!respawn)
    {
        return std::unexpected(Error::Unsupported("gamedata has no 'CCSPlayerController::Respawn' vtable index"));
    }

    respawn(_e);
    return {};
}

}  // namespace VoltMod
