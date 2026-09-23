#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <algorithm>
#include <bit>
#include <entity2/entityclass.h>
#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <schemasystem/schematypes.h>
#include <shareddefs.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant.h>

namespace VoltMod
{

static_assert(std::to_underlying(Team::None) == TEAM_UNASSIGNED);
static_assert(std::to_underlying(Team::Spectator) == TEAM_SPECTATOR);
static_assert(std::to_underlying(Team::T) == CS_TEAM_T);
static_assert(std::to_underlying(Team::CT) == CS_TEAM_CT);

// CS2's EmitSound_t; the SDK still declares the Source 1 layout. The engine reads it by offset.
struct EmitSoundParams
{
    const char* SoundName = nullptr;
    Vector SoundOrigin{0.0f, 0.0f, 0.0f};
    float Volume = 1.0f;
    float SoundTime = 0.0f;
    uint8_t Pad1C[0x4]{};
    uint32_t ForceGuid = 0;
    uint8_t Pad24[0x4]{};
    int16_t Pitch = 100;
    uint8_t Flags = 0;
};
static_assert(offsetof(EmitSoundParams, Volume) == 0x14);
static_assert(offsetof(EmitSoundParams, ForceGuid) == 0x20);
static_assert(offsetof(EmitSoundParams, Pitch) == 0x28);

// The filtered EmitSound returns this through the hidden sret ABI.
#pragma pack(push, 1)
struct StartSoundEventInfo
{
    uint32_t Guid;
    uint32_t StackHash;
    int32_t Flags;
    uint64_t Recipients;
};
#pragma pack(pop)
static_assert(sizeof(StartSoundEventInfo) == 20);

using EmitSoundFilterFn = StartSoundEventInfo (*)(IRecipientFilter& filter, CEntityIndex sourceIndex,
                                                  const EmitSoundParams& params);

static constexpr float MinScale = 0.05f;
static constexpr float MaxScale = 3.0f;

// The engine keeps neither pointer past the call.
static void FireInput(const Bindings& bindings, CEntityInstance* entity, std::string_view input, variant_t& value,
                      CEntityInstance* activator)
{
    if (!bindings.AcceptInput || !entity || input.empty())
    {
        return;
    }
    bindings.AcceptInput(entity, std::string(input).c_str(), activator, nullptr, &value);
}

// Origin and rotation live on the scene node, not on CBaseEntity.
static Schema::CGameSceneNode SceneNode(const Entity& entity)
{
    return entity.BodyComponent().SceneNode();
}

int Entity::Index() const
{
    return (_e && _e->m_pEntity) ? _e->GetEntityIndex().Get() : -1;
}

EntityRef Entity::Ref() const
{
    return {(_e && _e->m_pEntity) ? static_cast<uint32_t>(_e->GetRefEHandle().ToInt()) : EntityRef::Unset};
}

std::string_view Entity::ClassName() const
{
    if (!_e || !_e->m_pEntity)
    {
        return {};
    }
    const char* name = _e->GetClassname();
    return name ? std::string_view(name) : std::string_view{};
}

Vector Entity::Origin() const
{
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
    {
        return std::unexpected(Error::NotReady("no entity"));
    }

    const auto& teleport = _sys->Bindings().Teleport;
    if (!teleport)
    {
        return std::unexpected(Error::Unsupported("gamedata has no 'CBaseEntity::Teleport' vtable slot"));
    }

    teleport(_e, origin ? &*origin : nullptr, angles ? &*angles : nullptr, velocity ? &*velocity : nullptr);
    return {};
}

void Entity::SetMoveType(Schema::MoveType_t type) const
{
    SetMoveTypeRaw(type);
    SetActualMoveTypeRaw(type);
}

void Entity::Spawn(KeyValues& values) const
{
    if (_e && _sys && _sys->Bindings().DispatchSpawn)
    {
        _sys->Bindings().DispatchSpawn(_e, values.Detach());
    }
}

void Entity::AcceptInput(std::string_view input, std::string_view value, const Entity& activator) const
{
    if (!_sys)
    {
        return;
    }
    variant_t variant(std::string(value).c_str());
    FireInput(_sys->Bindings(), _e, input, variant, activator.Raw());
}

// Walks the entity's schema classes, since designer names such as "prop_dynamic" do not say it.
static bool IsModelEntity(CEntityInstance* entity)
{
    const CEntityIdentity* identity = entity ? entity->m_pEntity : nullptr;
    if (!identity || !identity->m_pClass || !identity->m_pClass->m_pClassInfo)
    {
        return false;
    }
    const CSchemaClassInfo* klass = identity->m_pClass->GetSchemaBinding();
    for (; klass; klass = klass->m_nBaseClassCount > 0 ? klass->m_pBaseClasses[0].m_pClass : nullptr)
    {
        if (klass->m_pszName && std::string_view(klass->m_pszName) == "CBaseModelEntity")
        {
            return true;
        }
    }
    return false;
}

void Entity::Remove() const
{
    CEntityIdentity* identity = _e ? _e->m_pEntity : nullptr;
    if (!identity || !identity->m_pClass)
    {
        return;
    }

    // The class's think lookup searches its bases, so every entity reaches CBaseEntity's removal.
    const BASEPTR remove = identity->m_pClass->m_NameToThinkFunc("CBaseEntitySUB_Remove");
    if (remove)
    {
        remove(_e);
    }
}

void Entity::RemoveAfter(float seconds) const
{
    if (!_e || !_sys || !_sys->Bindings().AddEntityIOEvent)
    {
        return;
    }
    CEntitySystem* system = _sys->Raw();
    if (!system)
    {
        return;
    }

    // The queue copies the input name and the value.
    const variant_t value("");
    _sys->Bindings().AddEntityIOEvent(system, _e, "Kill", nullptr, nullptr, &value, seconds, nullptr, nullptr);
}

void Entity::SetModel(std::string_view path) const
{
    if (_sys && _sys->Bindings().SetModel && !path.empty() && IsModelEntity(_e))
    {
        _sys->Bindings().SetModel(_e, std::string(path).c_str());
    }
}

void Entity::SetScale(float scale) const
{
    if (!_sys)
    {
        return;
    }
    variant_t value(std::clamp(scale, MinScale, MaxScale));
    FireInput(_sys->Bindings(), _e, "SetScale", value, nullptr);
}

void Entity::SetRender(Schema::RenderMode_t mode, Color color) const
{
    if (!IsModelEntity(_e))
    {
        return;
    }
    const Schema::CBaseModelEntity model{_e};
    model.SetRenderMode(mode);
    model.SetRenderColor(color);
}

void Entity::PlayAnimation(std::string_view animation, std::string_view idle) const
{
    if (!idle.empty())
    {
        AcceptInput("SetIdleAnimationLooping", idle);
    }
    AcceptInput("SetAnimationNotLooping", animation);
}

void Entity::EmitSound(std::string_view soundEvent, float volume) const
{
    if (_e && _sys && _sys->Bindings().EmitSoundParams && !soundEvent.empty())
    {
        _sys->Bindings().EmitSoundParams(_e, std::string(soundEvent).c_str(), 100, volume, 0.0f);
    }
}

void Entity::EmitSound(std::string_view soundEvent, IRecipientFilter& recipients, float volume) const
{
    if (!_e || !_sys || !_sys->Bindings().EmitSoundFilter || soundEvent.empty())
    {
        return;
    }

    // The params borrow the name; the engine reads it during the call.
    const std::string sound(soundEvent);
    EmitSoundParams params;
    params.SoundName = sound.c_str();
    params.Volume = volume;

    std::bit_cast<EmitSoundFilterFn>(_sys->Bindings().EmitSoundFilter.Ptr())(recipients, _e->GetEntityIndex(), params);
}

}  // namespace VoltMod
