#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <shareddefs.h>
#include <utility>

namespace VoltMod
{

static_assert(std::to_underlying(ObserverMode::None) == OBS_MODE_NONE);
static_assert(std::to_underlying(ObserverMode::Fixed) == OBS_MODE_FIXED);
static_assert(std::to_underlying(ObserverMode::InEye) == OBS_MODE_IN_EYE);
static_assert(std::to_underlying(ObserverMode::Chase) == OBS_MODE_CHASE);
static_assert(std::to_underlying(ObserverMode::Roaming) == OBS_MODE_ROAMING);

// Passed by value across the plugin boundary, so the layout is ABI.
static_assert(sizeof(Pawn) <= 160, "Pawn is a frame-local value; keep the field list tight.");

Vector Pawn::EyePosition() const
{
    return Origin() + ViewOffset();
}

void Pawn::SetMove(Schema::MoveType_t type) const
{
    SetMoveTypeRaw(type);
    SetActualMoveTypeRaw(type);
}

Status Pawn::Slay() const
{
    if (!_e || !_sys)
    {
        return std::unexpected(Error::NotReady("no pawn"));
    }

    const auto& suicide = _sys->Bindings().CommitSuicide;
    if (!suicide)
    {
        return std::unexpected(Error::Unsupported("gamedata has no 'CBasePlayerPawn::CommitSuicide' vtable index"));
    }

    suicide(_e, false, true);
    return {};
}

Pawn Entity::AsPawn() const
{
    return (_sys && ClassName() == "player") ? Pawn{*_sys, _e} : Pawn{};
}

VoltMod::ObserverMode Pawn::ObserverMode() const
{
    const Schema::CPlayer_ObserverServices services = ObserverServices();
    return services ? static_cast<VoltMod::ObserverMode>(services.ObserverMode()) : VoltMod::ObserverMode::None;
}

Status Pawn::SetObserverMode(VoltMod::ObserverMode value) const
{
    const Schema::CPlayer_ObserverServices services = ObserverServices();
    if (!services)
    {
        return std::unexpected(Error::NotReady("observer services unavailable"));
    }

    services.SetObserverMode(std::to_underlying(value));
    return {};
}

std::string Pawn::ModelName() const
{
    const Schema::CSkeletonInstance skeleton{BodyComponent().SceneNode().Base()};
    if (!skeleton)
    {
        return {};
    }

    const char* path = skeleton.ModelState().ModelName();
    return path ? std::string(path) : std::string{};
}

void Pawn::SetVisible(bool visible, uint8_t alpha) const
{
    const auto mode = visible ? Schema::RenderMode_t::kRenderNormal : Schema::RenderMode_t::kRenderTransAlpha;
    SetRender(mode, Color{.A = visible ? uint8_t{255} : alpha});
}

VoltMod::Controller Pawn::Controller() const
{
    return _sys ? _sys->Controller(Slot()) : VoltMod::Controller{};
}

int Pawn::Slot() const
{
    // Controllers sit at entity index slot + 1.
    const Entity controller = _sys ? _sys->Resolve(ControllerRef()) : Entity{};
    const int slot = controller.Index() - 1;
    return controller && IsValidSlot(slot) ? slot : -1;
}

}  // namespace VoltMod
