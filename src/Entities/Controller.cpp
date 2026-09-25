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
    _pawn = entities.Get(PlayerPawnRef()).Raw();
}

VoltMod::Pawn Controller::Pawn() const
{
    return _sys ? VoltMod::Pawn{*_sys, _pawn} : VoltMod::Pawn{};
}

VoltMod::Pawn Controller::InputPawn() const
{
    return _sys ? VoltMod::Pawn{*_sys, _sys->Get(PawnRef()).Raw()} : VoltMod::Pawn{};
}

uint64_t Controller::Buttons() const
{
    // m_pButtonStates[0] holds the buttons down this tick.
    const Schema::CPlayer_MovementServices services = InputPawn().MovementServices();
    return services ? services.Buttons().ButtonStates(0) : 0;
}

int Controller::Money() const
{
    const Schema::CCSPlayerController_InGameMoneyServices money = InGameMoneyServices();
    return money ? money.Account() : 0;
}

void Controller::SetMoney(int amount) const
{
    const Schema::CCSPlayerController_InGameMoneyServices money = InGameMoneyServices();
    if (money)
    {
        money.SetAccount(amount);
    }
}

void Controller::Kick(std::string_view reason) const
{
    if (!_e || !_sys || !_sys->Interfaces().Engine)
    {
        return;
    }

    const std::string text(reason);
    _sys->Interfaces().Engine->DisconnectClient(CPlayerSlot(_slot), NETWORK_DISCONNECT_KICKED, text.c_str());
}

void Controller::ChangeTeam(VoltMod::Team team) const
{
    const bool joinable = team == Team::Spectator || IsPlaying(team);
    if (_e && _sys && joinable && _sys->Bindings().ChangeTeam)
    {
        _sys->Bindings().ChangeTeam(_e, std::to_underlying(team));
    }
}

Status Controller::Respawn() const
{
    if (!_e || !_sys)
    {
        return std::unexpected(Error::NotReady("no controller"));
    }

    const auto& respawn = _sys->Bindings().Respawn;
    if (!respawn)
    {
        return std::unexpected(Error::Unsupported("gamedata has no 'CCSPlayerController::Respawn' vtable index"));
    }

    respawn(_e);
    return {};
}

}  // namespace VoltMod
