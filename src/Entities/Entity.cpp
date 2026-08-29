#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/Render.hpp>
#include <eiface.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <mathlib/vector.h>

namespace VoltMod
{

// Origin and rotation are not schema fields of CBaseEntity in CS2; they live on the entity's
// CGameSceneNode, reached via m_CBodyComponent -> m_pSceneNode.
static SchemaPtr SceneNode(CEntityInstance* entity)
{
    static const SchemaField<void> body{"CBaseEntity", "m_CBodyComponent"};
    static const SchemaField<void*> node{"CBodyComponent", "m_pSceneNode"};

    return SchemaPtr{entity}.At(body).Follow(node);
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
    static const SchemaField<Vector> origin{"CGameSceneNode", "m_vecAbsOrigin"};
    // Spelled out rather than left to `Vector{}`: the SDK's default constructor does not zero.
    return SceneNode(_e).Get(origin, Vector(0.0f, 0.0f, 0.0f));
}

QAngle Entity::Angles() const
{
    static const SchemaField<QAngle> rotation{"CGameSceneNode", "m_angAbsRotation"};
    return SceneNode(_e).Get(rotation, QAngle(0.0f, 0.0f, 0.0f));
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
    return Origin() + ViewOffset.Get();
}

void Pawn::SetMove(MoveType type) const
{
    const auto value = static_cast<uint8_t>(type);
    MoveTypeRaw = value;
    ActualMoveTypeRaw = value;
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

// The observer mode lives on a sub-object the pawn points at, so it is a method rather than a
// Field: there is no fixed offset from the pawn to reach it.
static SchemaPtr ObserverServices(CEntityInstance* pawn)
{
    static const SchemaField<void*> services{"CBasePlayerPawn", "m_pObserverServices"};
    return SchemaPtr{pawn}.Follow(services);
}

static const SchemaField<uint8_t> kObserverMode{"CPlayer_ObserverServices", "m_iObserverMode"};

ObserverMode_t Pawn::GetObserverMode() const
{
    return static_cast<ObserverMode_t>(
        ObserverServices(_e).Get(kObserverMode, static_cast<uint8_t>(ObserverMode_t::None)));
}

Status Pawn::SetObserverMode(ObserverMode_t value) const
{
    if (!ObserverServices(_e).Set(kObserverMode, static_cast<uint8_t>(value)))
        return std::unexpected(Error::NotReady("observer services unavailable"));
    return {};
}

std::string Pawn::ModelName() const
{
    // The pawn's scene node is a CSkeletonInstance; the model path is the CUtlSymbolLarge inside
    // its embedded CModelState (an interned string pointer).
    static const SchemaField<void> state{"CSkeletonInstance", "m_modelState"};
    static const SchemaField<const char*> name{"CModelState", "m_ModelName"};

    const char* path = SceneNode(_e).At(state).Get(name, nullptr);
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
    static const SchemaField<uint32_t> playerPawn{"CCSPlayerController", "m_hPlayerPawn"};

    _pawn = entities.Resolve(EntityRef{SchemaPtr{_e}.Get(playerPawn, InvalidEntityHandle)}).Raw();
}

Pawn Controller::GetPawn() const
{
    return _sys ? Pawn{*_sys, _pawn} : Pawn{};
}

Pawn Controller::Possessed() const
{
    static const SchemaField<uint32_t> possessedPawn{"CBasePlayerController", "m_hPawn"};

    if (!_sys)
        return {};
    return Pawn{*_sys, _sys->Resolve(EntityRef{SchemaPtr{_e}.Get(possessedPawn, InvalidEntityHandle)}).Raw()};
}

// The balance lives in a sub-object the controller points at, so it needs the same two-step reach
// as the observer mode above.
static const SchemaField<void*> kMoneyServices{"CCSPlayerController", "m_pInGameMoneyServices"};
static const SchemaField<int> kAccount{"CCSPlayerController_InGameMoneyServices", "m_iAccount"};

int Controller::Money() const
{
    return SchemaPtr{_e}.Follow(kMoneyServices).Get(kAccount);
}

Status Controller::SetMoney(int amount) const
{
    if (!SchemaPtr{_e}.Follow(kMoneyServices).Set(kAccount, amount))
        return std::unexpected(Error::NotReady("money services unavailable"));

    // The write is inside a sub-object, so it is invisible to the client on its own. Dirty the
    // controller's own pointer field, which is what the entity actually replicates through; the
    // HUD picks the new value up on the next update.
    MarkChanged(_e, *kMoneyServices);
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
