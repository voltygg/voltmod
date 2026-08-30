#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/Render.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <eiface.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <mathlib/vector.h>

namespace VoltMod
{

// Origin and rotation are not schema fields of CBaseEntity in CS2; they live on the entity's
// CGameSceneNode, reached via m_CBodyComponent -> m_pSceneNode.
static Schema::CGameSceneNode SceneNode(const Entity& entity)
{
    return entity.BodyComponent().SceneNode();
}

int Entity::Index() const
{
    return (_e && _e->m_pEntity) ? _e->m_pEntity->GetEntityIndex().Get() : -1;
}

EntityRef Entity::Ref() const
{
    return {(_e && _e->m_pEntity) ? static_cast<uint32_t>(_e->m_pEntity->m_EHandle.ToInt()) : InvalidEntityHandle};
}

std::string_view Entity::ClassName() const
{
    if (!_e || !_e->m_pEntity)
        return {};
    const char* name = _e->m_pEntity->m_designerName.String();
    return name ? std::string_view(name) : std::string_view{};
}

Vector Entity::Origin() const
{
    // Spelled out rather than left to `Vector{}`: the SDK's default constructor does not zero.
    const Schema::CGameSceneNode node = SceneNode(*this);
    return node ? node.AbsOrigin() : Vector(0.0f, 0.0f, 0.0f);
}

QAngle Entity::Angles() const
{
    const Schema::CGameSceneNode node = SceneNode(*this);
    return node ? node.AbsRotation() : QAngle(0.0f, 0.0f, 0.0f);
}

Status Entity::Teleport(std::optional<Vector> origin, std::optional<QAngle> angles,
                        std::optional<Vector> velocity) const
{
    if (!_e || !_sys)
        return std::unexpected(Error::NotReady("no entity"));

    const auto& teleport = _sys->BindingsRef().Teleport;
    if (!teleport)
        return std::unexpected(Error::Unsupported("gamedata has no 'Teleport' vtable index"));

    teleport.Call(_e, origin ? &*origin : nullptr, angles ? &*angles : nullptr, velocity ? &*velocity : nullptr);
    return {};
}

Vector Pawn::EyePosition() const
{
    return Origin() + ViewOffset();
}

void Pawn::SetMove(MoveType type) const
{
    const auto value = static_cast<Schema::MoveType_t>(type);
    SetMoveTypeRaw(value);
    SetActualMoveTypeRaw(value);
}

Status Pawn::Slay() const
{
    if (!_e || !_sys)
        return std::unexpected(Error::NotReady("no pawn"));

    const auto& suicide = _sys->BindingsRef().CommitSuicide;
    if (!suicide)
        return std::unexpected(Error::Unsupported("gamedata has no 'CommitSuicide' vtable index"));

    suicide.Call(_e, false, true);
    return {};
}

ObserverMode_t Pawn::GetObserverMode() const
{
    const Schema::CPlayer_ObserverServices services = ObserverServices();
    return services ? static_cast<ObserverMode_t>(services.ObserverMode()) : ObserverMode_t::None;
}

Status Pawn::SetObserverMode(ObserverMode_t value) const
{
    const Schema::CPlayer_ObserverServices services = ObserverServices();
    if (!services)
        return std::unexpected(Error::NotReady("observer services unavailable"));

    services.SetObserverMode(static_cast<uint8_t>(value));
    return {};
}

std::string Pawn::ModelName() const
{
    // The pawn's scene node is a CSkeletonInstance; the model path is the CUtlSymbolLarge inside
    // its embedded CModelState (an interned string pointer).
    const Schema::CSkeletonInstance skeleton{SceneNode(*this).Base()};
    if (!skeleton)
        return {};

    const char* path = skeleton.ModelState().ModelName();
    return path ? std::string(path) : std::string{};
}

void Pawn::SetRender(RenderMode_t mode, uint32_t color) const
{
    // Qualified: this member would otherwise hide the free function of the same name.
    VoltMod::SetRender(_e, mode, color);
}

void Pawn::SetVisible(bool visible, uint8_t alpha) const
{
    RenderMode_t mode = visible ? RenderMode_t::Normal : RenderMode_t::TransTexture;
    // m_clrRender packs alpha in the top byte; the low three bytes stay opaque white.
    uint32_t color = visible ? ColorOpaqueWhite : ((static_cast<uint32_t>(alpha) << 24) | 0x00FFFFFFu);
    SetRender(mode, color);
}

Controller Pawn::GetController() const
{
    if (!_sys)
        return {};
    return _sys->Controller(Slot());
}

int Pawn::Slot() const
{
    return _sys ? _sys->SlotOf(*this) : -1;
}

Controller::Controller(EntitySystem& entities, CEntityInstance* raw, int slot) : Entity(entities, raw), _slot(slot)
{
    _pawn = entities.Resolve(EntityRef{PlayerPawnHandle()}).Raw();
}

Pawn Controller::GetPawn() const
{
    return _sys ? Pawn{*_sys, _pawn} : Pawn{};
}

Pawn Controller::Possessed() const
{
    if (!_sys)
        return {};
    return Pawn{*_sys, _sys->Resolve(EntityRef{PawnHandle()}).Raw()};
}

int Controller::Money() const
{
    const Schema::CCSPlayerController_InGameMoneyServices money = InGameMoneyServices();
    return money ? money.Account() : 0;
}

Status Controller::SetMoney(int amount) const
{
    // The money services carry their own __m_pChainEntity, so the generated setter dirties the
    // controller through that chainer; there is no outer pointer field to mark by hand.
    const Schema::CCSPlayerController_InGameMoneyServices money = InGameMoneyServices();
    if (!money)
        return std::unexpected(Error::NotReady("money services unavailable"));

    money.SetAccount(amount);
    return {};
}

Status Controller::Kick(std::string_view reason) const
{
    if (!_e || !_sys)
        return std::unexpected(Error::NotReady("no controller"));

    auto* engine = _sys->InterfacesRef().Engine;
    if (!engine)
        return std::unexpected(Error::NotReady("IVEngineServer2 not available"));

    // DisconnectClient takes a C string; the reason is short and this is not a hot path.
    const std::string text(reason);
    engine->DisconnectClient(CPlayerSlot(_slot), NETWORK_DISCONNECT_KICKED, text.c_str());
    return {};
}

Status Controller::ChangeTeam(int team) const
{
    if (!_e || !_sys)
        return std::unexpected(Error::NotReady("no controller"));

    const auto& changeTeam = _sys->BindingsRef().ChangeTeam;
    if (!changeTeam)
        return std::unexpected(Error::Unsupported("gamedata has no 'ChangeTeam' vtable index"));

    changeTeam.Call(_e, team);
    return {};
}

Status Controller::Respawn() const
{
    if (!_e || !_sys)
        return std::unexpected(Error::NotReady("no controller"));

    const auto& respawn = _sys->BindingsRef().Respawn;
    if (!respawn)
        return std::unexpected(Error::Unsupported("gamedata has no 'Respawn' vtable index"));

    respawn.Call(_e);
    return {};
}

// Wrappers repeat the entity pointer in each Field and are passed by value. Keep these size checks
// close to the fields so adding one requires an intentional update.
static_assert(sizeof(Pawn) <= 160, "Pawn is a frame-local value; keep the field list tight.");
static_assert(sizeof(Controller) <= 104, "Controller is a frame-local value; keep the field list tight.");

}  // namespace VoltMod
