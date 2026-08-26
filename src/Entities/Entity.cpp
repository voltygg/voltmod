#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
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
static void* ResolveSceneNode(CEntityInstance* entity)
{
    static const LazyField body{"CBaseEntity", "m_CBodyComponent"};
    static const LazyField node{"CBodyComponent", "m_pSceneNode", sizeof(void*)};

    if (!entity || !body || !node)
        return nullptr;

    auto* component = ReadAt<uint8_t*>(entity, body->Offset);
    return component ? ReadAt<void*>(component, node->Offset) : nullptr;
}

template <typename T>
static T SceneNodeField(CEntityInstance* entity, const LazyField& field)
{
    void* node = ResolveSceneNode(entity);
    if (!node || !field)
        return T{0.0f, 0.0f, 0.0f};
    return ReadAt<T>(node, field->Offset);
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
    static const LazyField origin{"CGameSceneNode", "m_vecAbsOrigin", sizeof(Vector)};
    return SceneNodeField<Vector>(_e, origin);
}

QAngle Entity::Angles() const
{
    static const LazyField rotation{"CGameSceneNode", "m_angAbsRotation", sizeof(QAngle)};
    return SceneNodeField<QAngle>(_e, rotation);
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
static void* ObserverServices(CEntityInstance* pawn)
{
    static const LazyField services{"CBasePlayerPawn", "m_pObserverServices", sizeof(void*)};
    if (!pawn || !services)
        return nullptr;
    return ReadAt<void*>(pawn, services->Offset);
}

ObserverMode_t Pawn::GetObserverMode() const
{
    static const LazyField mode{"CPlayer_ObserverServices", "m_iObserverMode", sizeof(uint8_t)};

    void* services = ObserverServices(_e);
    if (!services || !mode)
        return ObserverMode_t::None;
    return static_cast<ObserverMode_t>(ReadAt<uint8_t>(services, mode->Offset));
}

Status Pawn::SetObserverMode(ObserverMode_t value) const
{
    static const LazyField mode{"CPlayer_ObserverServices", "m_iObserverMode", sizeof(uint8_t)};

    void* services = ObserverServices(_e);
    if (!services || !mode)
        return std::unexpected(Error::NotReady("observer services unavailable"));

    WriteAt<uint8_t>(services, mode->Offset, static_cast<uint8_t>(value));
    return {};
}

std::string Pawn::ModelName() const
{
    // The pawn's scene node is a CSkeletonInstance; the model path is the CUtlSymbolLarge inside
    // its embedded CModelState (an interned string pointer).
    static const LazyField state{"CSkeletonInstance", "m_modelState"};
    static const LazyField name{"CModelState", "m_ModelName"};

    void* node = ResolveSceneNode(_e);
    if (!node || !state || !name)
        return {};

    const char* path = ReadAt<const char*>(node, state->Offset + name->Offset);
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
    static const LazyField playerPawn{"CCSPlayerController", "m_hPlayerPawn", sizeof(uint32_t)};

    if (_e && playerPawn)
        _pawn = entities.Resolve(EntityRef{ReadAt<uint32_t>(_e, playerPawn->Offset)}).Raw();
}

Pawn Controller::GetPawn() const
{
    return _sys ? Pawn{*_sys, _pawn} : Pawn{};
}

// The balance lives in a sub-object the controller points at, so it needs the same two-step reach
// as the observer mode above.
static void* MoneyServices(CEntityInstance* controller)
{
    static const LazyField services{"CCSPlayerController", "m_pInGameMoneyServices", sizeof(void*)};
    if (!controller || !services)
        return nullptr;
    return ReadAt<void*>(controller, services->Offset);
}

int Controller::Money() const
{
    static const LazyField account{"CCSPlayerController_InGameMoneyServices", "m_iAccount", sizeof(int)};

    void* services = MoneyServices(_e);
    if (!services || !account)
        return 0;
    return ReadAt<int>(services, account->Offset);
}

Status Controller::SetMoney(int amount) const
{
    static const LazyField servicesField{"CCSPlayerController", "m_pInGameMoneyServices", sizeof(void*)};
    static const LazyField account{"CCSPlayerController_InGameMoneyServices", "m_iAccount", sizeof(int)};

    void* services = MoneyServices(_e);
    if (!services || !account)
        return std::unexpected(Error::NotReady("money services unavailable"));

    WriteAt<int>(services, account->Offset, amount);

    // The write is inside a sub-object, so it is invisible to the client on its own. Dirty the
    // controller's own pointer field, which is what the entity actually replicates through; the
    // HUD picks the new value up on the next update.
    if (servicesField)
        MarkChanged(_e, *servicesField);
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

// Each wrapper repeats the entity pointer once per Field, so its size is the field count: today
// Entity is 72 bytes (2 pointers + 7 fields), Pawn 152 and Controller 96. These are values, copied
// into every call that takes one, so the bounds are here to make adding a field deliberate - raise
// them with the field, do not widen them in advance.
static_assert(sizeof(Pawn) <= 160, "Pawn is a frame-local value; keep the field list tight.");
static_assert(sizeof(Controller) <= 104, "Controller is a frame-local value; keep the field list tight.");

}  // namespace VoltMod
