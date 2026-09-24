#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <algorithm>
#include <entityhandle.h>
#include <shareddefs.h>
#include <string>
#include <tier1/utlvector.h>
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

void Pawn::SetGodmode(bool on) const
{
    SetFlags(on ? (Flags() | FL_GODMODE) : (Flags() & ~FL_GODMODE));
}

void Pawn::Launch(Vector velocity) const
{
    // Written directly: a Teleport carrying only a velocity has crashed CS2 builds.
    SetVelocity(velocity);
    // Otherwise the engine grounds the pawn again on the next tick.
    SetFlags(Flags() & ~FL_ONGROUND);
}

bool Pawn::Heal(int amount) const
{
    const int health = Health();
    const int healed = std::min(health + amount, MaxHealth());
    if (!IsAlive() || healed <= health)
    {
        return false;
    }
    SetHealth(healed);
    return true;
}

bool Pawn::GiveItem(std::string_view item) const
{
    const Schema::CPlayer_ItemServices services = ItemServices();
    if (!_sys || !services || item.empty())
    {
        return false;
    }
    const auto& give = _sys->Bindings().GiveNamedItem;
    if (!give)
    {
        return false;
    }

    const std::string className(item);
    if (give(services.Base(), className.c_str()))
    {
        return true;
    }

    // The engine refuses a weapon only the other team can buy; the swap is undone before anyone sees it.
    const VoltMod::Team team = Team();
    const VoltMod::Team other = Opposite(team);
    if (other == VoltMod::Team::None)
    {
        return false;
    }
    SetTeam(other);
    const bool given = give(services.Base(), className.c_str()) != nullptr;
    SetTeam(team);

    if (!given)
    {
        Log::Warn("The engine refused '{}' for both teams.", item);
    }
    return given;
}

bool Pawn::StripWeapons(bool removeSuit) const
{
    const Schema::CPlayer_ItemServices services = ItemServices();
    if (!_sys || !services || !_sys->Bindings().RemoveAllItems)
    {
        return false;
    }
    _sys->Bindings().RemoveAllItems(services.Base(), removeSuit);
    return true;
}

std::vector<Entity> Pawn::Weapons() const
{
    std::vector<Entity> weapons;
    const Schema::CPlayer_WeaponServices services = WeaponServices();
    // The schema's CNetworkUtlVectorBase<CHandle<T>> is laid out as a CUtlVector.
    const auto* handles = services ? static_cast<const CUtlVector<CEntityHandle>*>(services.MyWeapons()) : nullptr;
    if (!_sys || !handles)
    {
        return weapons;
    }
    for (int i = 0; i < handles->Count(); ++i)
    {
        if (const Entity weapon = _sys->Get(EntityRef{static_cast<uint32_t>(handles->Element(i).ToInt())}))
        {
            weapons.push_back(weapon);
        }
    }
    return weapons;
}

void Pawn::HoldFire(int tick) const
{
    const Schema::CPlayer_WeaponServices services = WeaponServices();
    const Entity active = services && _sys ? _sys->Get(services.ActiveWeaponRef()) : Entity{};
    const Schema::CBasePlayerWeapon weapon{active.Raw()};
    if (!weapon)
    {
        return;
    }
    if (weapon.NextPrimaryAttackTick() < tick)
    {
        weapon.SetNextPrimaryAttackTick(tick);
    }
    if (weapon.NextSecondaryAttackTick() < tick)
    {
        weapon.SetNextSecondaryAttackTick(tick);
    }
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
    const Entity controller = _sys ? _sys->Get(ControllerRef()) : Entity{};
    const int slot = controller.Index() - 1;
    return controller && IsValidSlot(slot) ? slot : -1;
}

}  // namespace VoltMod
