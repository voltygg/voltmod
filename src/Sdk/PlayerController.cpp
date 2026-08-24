#include "Sdk/Schema.hpp"
#include "Sdk/VirtualCall.hpp"

#include <CS2Kit/Core/Log.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/Entity.hpp>
#include <CS2Kit/Sdk/EntityOps.hpp>
#include <CS2Kit/Sdk/EntityRender.hpp>
#include <CS2Kit/Sdk/GameData.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <CS2Kit/Sdk/MemoryAccess.hpp>
#include <CS2Kit/Sdk/PlayerController.hpp>
#include <algorithm>
#include <cstring>
#include <eiface.h>
#include <entity2/entityinstance.h>
#include <mathlib/vector.h>

namespace CS2Kit::Sdk
{

using namespace CS2Kit::Core;

namespace
{
// Resolve a vtable index by its gamedata name and call it on `target`. No-op (with a warning)
// when the offset is missing or `target` is null - collapses the lookup/guard/dispatch the
// vtable wrappers all repeat.
template <typename... Args>
void CallVtableByName(void* target, const char* name, Args... args)
{
    if (!target)
        return;
    int index = CS2Kit::Detail::Rt().GameData.GetOffset(name);
    if (index < 0)
    {
        Log::Warn("PlayerController: vtable offset '{}' not found.", name);
        return;
    }
    CallVirtual<void>(index, target, args...);
}

// Origin/rotation are not schema fields of CBaseEntity in CS2; they live on the
// pawn's CGameSceneNode, reached via m_CBodyComponent -> m_pSceneNode.
void* ResolveSceneNode(CEntityInstance* pawn)
{
    if (!pawn)
        return nullptr;

    int bodyOffset = CS2Kit::Detail::Rt().Schema().GetOffset("CBaseEntity", "m_CBodyComponent");
    if (bodyOffset < 0)
        return nullptr;
    auto* body = ReadAt<uint8_t*>(pawn, bodyOffset);
    if (!body)
        return nullptr;

    int nodeOffset = CS2Kit::Detail::Rt().Schema().GetOffsetOf<void*>("CBodyComponent", "m_pSceneNode");
    if (nodeOffset < 0)
        return nullptr;
    return ReadAt<void*>(body, nodeOffset);
}

template <typename T>
T GetSceneNodeField(CEntityInstance* pawn, const char* fieldName)
{
    void* node = ResolveSceneNode(pawn);
    if (!node)
        return T{0.0f, 0.0f, 0.0f};

    int offset = CS2Kit::Detail::Rt().Schema().GetOffsetOf<T>("CGameSceneNode", fieldName);
    if (offset < 0)
        return T{0.0f, 0.0f, 0.0f};
    return ReadAt<T>(node, offset);
}
}  // namespace

PlayerController::PlayerController(int slot) : _slot(slot)
{
    _controller = CS2Kit::Detail::Rt().Entities.GetPlayerController(slot);
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

    int offset = CS2Kit::Detail::Rt().Schema().GetOffsetOf<uint32_t>("CCSPlayerController", "m_hPlayerPawn");
    if (offset < 0)
        return nullptr;

    auto hPawn = ReadAt<uint32_t>(_controller, offset);
    return CS2Kit::Detail::Rt().Entities.ResolveEntityHandle(hPawn);
}

void PlayerController::Kick(const char* reason) const
{
    if (!IsValid())
        return;

    auto* engine = CS2Kit::Detail::Rt().Interfaces.Engine;
    if (!engine)
    {
        Log::Warn("PlayerController::Kick: IVEngineServer2 not available.");
        return;
    }

    engine->DisconnectClient(CPlayerSlot(_slot), NETWORK_DISCONNECT_KICKED, reason);
}

int PlayerController::SchemaOffset(const char* className, const char* fieldName, int expectedSize) const
{
    return CS2Kit::Detail::Rt().Schema().GetOffset(className, fieldName, expectedSize);
}

int PlayerController::GetHealth() const
{
    return GetPawnField<int>("CBaseEntity", "m_iHealth");
}

int PlayerController::GetTeam() const
{
    return GetPawnField<int>("CBaseEntity", "m_iTeamNum");
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
    return CS2Kit::Detail::Rt().Entities.GetPlayerButtons(_slot);
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

void PlayerController::SetSpeedModifier(float multiplier) const
{
    SetPawnField<float>("CCSPlayerPawn", "m_flVelocityModifier", multiplier);
}

void PlayerController::SetModelScale(float scale) const
{
    // Hard clamp: very large model scales blow up the collision hull and can destabilize
    // or crash the server. This is the crash-safety bound, not a gameplay ceiling; keep every
    // caller inside it regardless of input.
    constexpr float MinSafeModelScale = 0.05f;
    constexpr float MaxSafeModelScale = 3.0f;
    scale = std::clamp(scale, MinSafeModelScale, MaxSafeModelScale);
    CS2Kit::Detail::Rt().EntityOps.AcceptInputFloat(GetPawn(), "SetScale", scale);
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
    return GetSceneNodeField<Vector>(GetPawn(), "m_vecAbsOrigin");
}

QAngle PlayerController::GetAbsAngles() const
{
    return GetSceneNodeField<QAngle>(GetPawn(), "m_angAbsRotation");
}

QAngle PlayerController::GetEyeAngles() const
{
    // The schema declares m_angEyeAngles on CCSPlayerPawn and the lookup does not walk base classes:
    // asking for it on CCSPlayerPawnBase resolves nothing and reads (0,0,0).
    return GetPawnField<QAngle>("CCSPlayerPawn", "m_angEyeAngles");
}

Vector PlayerController::GetEyePosition() const
{
    return GetAbsOrigin() + GetPawnField<Vector>("CBaseModelEntity", "m_vecViewOffset");
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
    CallVtableByName(GetPawn(), "CommitSuicide", false, true);
}

void PlayerController::ChangeTeam(int team) const
{
    CallVtableByName(_controller, "ChangeTeam", team);
}

void PlayerController::Respawn() const
{
    CallVtableByName(_controller, "Respawn");
}

void PlayerController::Teleport(const Vector* origin, const QAngle* angles, const Vector* velocity) const
{
    CallVtableByName(GetPawn(), "Teleport", origin, angles, velocity);
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

    int offset = CS2Kit::Detail::Rt().Schema().GetOffset("CBasePlayerController", "m_iszPlayerName");
    if (offset < 0)
        return {};

    auto* p = MemberPtr<const char>(_controller, offset);
    size_t len = 0;
    while (len < PlayerNameBufferSize && p[len] != '\0')
        ++len;
    return std::string(p, len);
}

std::string PlayerController::GetPawnModelName() const
{
    // The pawn's scene node is a CSkeletonInstance; the model path is the
    // CUtlSymbolLarge inside its embedded CModelState (interned string pointer).
    void* node = ResolveSceneNode(GetPawn());
    if (!node)
        return {};

    auto& schema = CS2Kit::Detail::Rt().Schema();
    int stateOffset = schema.GetOffset("CSkeletonInstance", "m_modelState");
    int nameOffset = schema.GetOffset("CModelState", "m_ModelName");
    if (stateOffset < 0 || nameOffset < 0)
        return {};

    const char* name = ReadAt<const char*>(node, stateOffset + nameOffset);
    return name ? std::string(name) : std::string{};
}

void PlayerController::SetPlayerName(const std::string& name) const
{
    if (!_controller)
        return;

    int offset = CS2Kit::Detail::Rt().Schema().GetOffset("CBasePlayerController", "m_iszPlayerName");
    if (offset < 0)
        return;

    auto* dst = MemberPtr<char>(_controller, offset);
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

    SetEntityRender(pawn, mode, color);
}

}  // namespace CS2Kit::Sdk
