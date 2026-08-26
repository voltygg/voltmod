#include "Engine/VirtualCall.hpp"
#include "Entities/Schema.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/PlayerController.hpp>
#include <VoltMod/Entities/Render.hpp>
#include <cstring>
#include <eiface.h>
#include <entity2/entityinstance.h>
#include <mathlib/vector.h>
#include <utility>

namespace VoltMod::Entities
{

using namespace VoltMod::Core;

namespace
{
// Resolve a vtable index by its gamedata name and call it on `target`. No-op (with a warning)
// when the offset is missing or `target` is null - collapses the lookup/guard/dispatch the
// vtable wrappers all repeat.
template <typename... Args>
void CallVtableByName(const Engine::GameData& gameData, void* target, const char* name, Args&&... args)
{
    if (!target)
        return;
    int index = gameData.GetVtableIndex(name);
    if (index < 0)
        return;
    Engine::CallVirtual<void>(index, target, std::forward<Args>(args)...);
}

// Origin/rotation are not schema fields of CBaseEntity in CS2; they live on the
// pawn's CGameSceneNode, reached via m_CBodyComponent -> m_pSceneNode.
void* ResolveSceneNode(SchemaService& schema, CEntityInstance* pawn)
{
    if (!pawn)
        return nullptr;

    int bodyOffset = schema.GetOffset("CBaseEntity", "m_CBodyComponent");
    if (bodyOffset < 0)
        return nullptr;
    auto* body = Engine::ReadAt<uint8_t*>(pawn, bodyOffset);
    if (!body)
        return nullptr;

    int nodeOffset = schema.GetOffsetOf<void*>("CBodyComponent", "m_pSceneNode");
    if (nodeOffset < 0)
        return nullptr;
    return Engine::ReadAt<void*>(body, nodeOffset);
}

template <typename T>
T GetSceneNodeField(SchemaService& schema, CEntityInstance* pawn, const char* fieldName)
{
    void* node = ResolveSceneNode(schema, pawn);
    if (!node)
        return T{0.0f, 0.0f, 0.0f};

    int offset = schema.GetOffsetOf<T>("CGameSceneNode", fieldName);
    if (offset < 0)
        return T{0.0f, 0.0f, 0.0f};
    return Engine::ReadAt<T>(node, offset);
}
}  // namespace

PlayerController EntitySystem::Controller(int slot)
{
    return PlayerController(*this, slot);
}

PlayerController::PlayerController(EntitySystem& entities, int slot) : _entities(&entities), _slot(slot)
{
    _controller = entities.GetPlayerController(slot);
}

bool PlayerController::IsValid() const
{
    return _controller != nullptr;
}

CEntityInstance* PlayerController::GetEntity() const
{
    return _controller;
}

CEntityInstance* PlayerController::GetPawn() const
{
    if (!_controller)
        return nullptr;

    int offset = _entities->Schema().GetOffsetOf<uint32_t>("CCSPlayerController", "m_hPlayerPawn");
    if (offset < 0)
        return nullptr;

    auto hPawn = Engine::ReadAt<uint32_t>(_controller, offset);
    return _entities->ResolveEntityHandle(hPawn);
}

void PlayerController::Kick(const char* reason) const
{
    if (!IsValid())
        return;

    auto* engine = _entities->Interfaces().Engine;
    if (!engine)
    {
        Log::Warn("PlayerController::Kick: IVEngineServer2 not available.");
        return;
    }

    engine->DisconnectClient(CPlayerSlot(_slot), NETWORK_DISCONNECT_KICKED, reason);
}

int PlayerController::SchemaOffset(const char* className, const char* fieldName, int expectedSize) const
{
    return _entities->Schema().GetOffset(className, fieldName, expectedSize);
}

int PlayerController::GetHealth() const
{
    return GetPawnField<int>("CBaseEntity", "m_iHealth");
}

int PlayerController::GetTeam() const
{
    // m_iTeamNum is one byte; an int read also picked up the three that follow.
    return GetPawnField<uint8_t>("CBaseEntity", "m_iTeamNum");
}

int PlayerController::GetLifeState() const
{
    return GetPawnField<uint8_t>("CBaseEntity", "m_lifeState");
}

bool PlayerController::IsAlive() const
{
    return GetPawn() != nullptr && GetLifeState() == 0;
}

uint64_t PlayerController::GetButtons() const
{
    return _entities->GetPlayerButtons(_slot);
}

void PlayerController::SetHealth(int health) const
{
    SetPawnField<int>("CBaseEntity", "m_iHealth", health);
}

int PlayerController::GetArmor() const
{
    return GetPawnField<int>("CCSPlayerPawn", "m_ArmorValue");
}

void PlayerController::SetArmor(int armor) const
{
    SetPawnField<int>("CCSPlayerPawn", "m_ArmorValue", armor);
}

namespace
{
constexpr const char* MoneyServicesField = "m_pInGameMoneyServices";
constexpr const char* MoneyClass = "CCSPlayerController_InGameMoneyServices";
constexpr const char* MoneyField = "m_iAccount";
}  // namespace

int PlayerController::GetMoney() const
{
    auto* services = GetField<void*>("CCSPlayerController", MoneyServicesField);
    if (!services)
        return 0;
    int offset = SchemaOffset(MoneyClass, MoneyField, sizeof(int));
    if (offset < 0)
        return 0;
    return Engine::ReadAt<int>(services, offset);
}

void PlayerController::SetMoney(int amount) const
{
    auto* services = GetField<void*>("CCSPlayerController", MoneyServicesField);
    if (!services)
        return;
    int offset = SchemaOffset(MoneyClass, MoneyField, sizeof(int));
    if (offset < 0)
        return;
    Engine::WriteAt<int>(services, offset, amount);

    // The balance lives in a sub-object, so the write alone is invisible to the client. Dirty
    // the controller's own pointer field, which is what the entity actually replicates through;
    // the HUD picks the new value up on the next update.
    if (_controller)
    {
        int servicesOffset = SchemaOffset("CCSPlayerController", MoneyServicesField, sizeof(void*));
        if (servicesOffset >= 0)
            _controller->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(servicesOffset)));
    }
}

void PlayerController::SetSpeedModifier(float multiplier) const
{
    SetPawnField<float>("CCSPlayerPawn", "m_flVelocityModifier", multiplier);
}

uint32_t PlayerController::GetFlags() const
{
    return GetPawnField<uint32_t>("CBaseEntity", "m_fFlags");
}

void PlayerController::SetFlags(uint32_t flags) const
{
    SetPawnField<uint32_t>("CBaseEntity", "m_fFlags", flags);
}

Vector PlayerController::GetVelocity() const
{
    return GetPawnField<Vector>("CBaseEntity", "m_vecAbsVelocity");
}

void PlayerController::SetVelocity(const Vector& velocity) const
{
    SetPawnField<Vector>("CBaseEntity", "m_vecAbsVelocity", velocity);
}

uint8_t PlayerController::GetRenderMode() const
{
    return GetPawnField<uint8_t>("CBaseModelEntity", "m_nRenderMode");
}

uint32_t PlayerController::GetRenderColor() const
{
    return GetPawnField<uint32_t>("CBaseModelEntity", "m_clrRender");
}

void PlayerController::SetRender(uint8_t mode, uint32_t color) const
{
    SetPawnField<uint8_t>("CBaseModelEntity", "m_nRenderMode", mode);
    SetPawnField<uint32_t>("CBaseModelEntity", "m_clrRender", color);
}

Vector PlayerController::GetAbsOrigin() const
{
    return GetSceneNodeField<Vector>(_entities->Schema(), GetPawn(), "m_vecAbsOrigin");
}

QAngle PlayerController::GetAbsAngles() const
{
    return GetSceneNodeField<QAngle>(_entities->Schema(), GetPawn(), "m_angAbsRotation");
}

QAngle PlayerController::GetEyeAngles() const
{
    // The schema declares m_angEyeAngles on CCSPlayerPawn and the lookup does not walk base classes:
    // asking for it on CCSPlayerPawnBase resolves nothing and reads (0,0,0).
    return GetPawnField<QAngle>("CCSPlayerPawn", "m_angEyeAngles");
}

Vector PlayerController::GetEyePosition() const
{
    // m_vecViewOffset is a 40-byte CNetworkViewOffsetVector led by the Vector, so no size check.
    return GetAbsOrigin() + GetPawnField<Vector>("CBaseModelEntity", "m_vecViewOffset", 0);
}

float PlayerController::GetFlashDuration() const
{
    return GetPawnField<float>("CCSPlayerPawnBase", "m_flFlashDuration");
}

float PlayerController::GetFlashMaxAlpha() const
{
    return GetPawnField<float>("CCSPlayerPawnBase", "m_flFlashMaxAlpha");
}

void PlayerController::Slay() const
{
    CallVtableByName(_entities->GameDataRef(), GetPawn(), "CommitSuicide", false, true);
}

void PlayerController::ChangeTeam(int team) const
{
    CallVtableByName(_entities->GameDataRef(), _controller, "ChangeTeam", team);
}

void PlayerController::Respawn() const
{
    CallVtableByName(_entities->GameDataRef(), _controller, "Respawn");
}

void PlayerController::Teleport(const Vector* origin, const QAngle* angles, const Vector* velocity) const
{
    CallVtableByName(_entities->GameDataRef(), GetPawn(), "Teleport", origin, angles, velocity);
}

namespace
{
// Player name is stored as a 128-byte fixed buffer on CBasePlayerController.
constexpr size_t PlayerNameBufferSize = 128;
}  // namespace

MoveType PlayerController::GetMoveType() const
{
    return static_cast<MoveType>(GetPawnField<uint8_t>("CBaseEntity", "m_MoveType"));
}

void PlayerController::SetMoveType(MoveType type) const
{
    auto value = static_cast<uint8_t>(type);
    SetPawnField<uint8_t>("CBaseEntity", "m_MoveType", value);
    SetPawnField<uint8_t>("CBaseEntity", "m_nActualMoveType", value);
}

ObserverMode_t PlayerController::GetObserverMode() const
{
    return static_cast<ObserverMode_t>(GetPawnField<uint8_t>("CPlayer_ObserverServices", "m_iObserverMode"));
}

void PlayerController::SetObserverMode(ObserverMode_t mode) const
{
    SetPawnField<uint8_t>("CPlayer_ObserverServices", "m_iObserverMode", static_cast<uint8_t>(mode));
}

std::string PlayerController::GetPlayerName() const
{
    if (!_controller)
        return {};

    int offset = _entities->Schema().GetOffset("CBasePlayerController", "m_iszPlayerName");
    if (offset < 0)
        return {};

    auto* p = Engine::MemberPtr<const char>(_controller, offset);
    size_t len = 0;
    while (len < PlayerNameBufferSize && p[len] != '\0')
        ++len;
    return std::string(p, len);
}

std::string PlayerController::GetPawnModelName() const
{
    // The pawn's scene node is a CSkeletonInstance; the model path is the
    // CUtlSymbolLarge inside its embedded CModelState (interned string pointer).
    auto& schema = _entities->Schema();
    void* node = ResolveSceneNode(schema, GetPawn());
    if (!node)
        return {};

    int stateOffset = schema.GetOffset("CSkeletonInstance", "m_modelState");
    int nameOffset = schema.GetOffset("CModelState", "m_ModelName");
    if (stateOffset < 0 || nameOffset < 0)
        return {};

    const char* name = Engine::ReadAt<const char*>(node, stateOffset + nameOffset);
    return name ? std::string(name) : std::string{};
}

void PlayerController::SetPlayerName(const std::string& name) const
{
    if (!_controller)
        return;

    int offset = _entities->Schema().GetOffset("CBasePlayerController", "m_iszPlayerName");
    if (offset < 0)
        return;

    auto* dst = Engine::MemberPtr<char>(_controller, offset);
    std::memset(dst, 0, PlayerNameBufferSize);
    size_t copyLen = name.size();
    if (copyLen >= PlayerNameBufferSize)
        copyLen = PlayerNameBufferSize - 1;
    if (copyLen > 0)
        std::memcpy(dst, name.data(), copyLen);
}

void PlayerController::SetVisible(bool visible, uint8_t alpha) const
{
    auto* pawn = GetPawn();
    if (!pawn)
        return;

    RenderMode_t mode = visible ? RenderMode_t::Normal : RenderMode_t::TransTexture;
    // m_clrRender packs alpha in the top byte; the low three bytes stay opaque white.
    uint32_t color = visible ? ColorOpaqueWhite : ((static_cast<uint32_t>(alpha) << 24) | 0x00FFFFFFu);

    // Qualified: the PlayerController::SetRender overload above would otherwise hide the free one.
    Entities::SetRender(_entities->Schema(), pawn, mode, color);
}

}  // namespace VoltMod::Entities
