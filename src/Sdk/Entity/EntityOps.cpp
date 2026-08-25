#include "Sdk/Internal/Schema.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Sdk/Engine/GameData.hpp>
#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <VoltMod/Sdk/Entity/EntityKeyValues.hpp>
#include <VoltMod/Sdk/Entity/EntityOps.hpp>
#include <bit>
#include <entity2/entityinstance.h>
#include <entity2/entitykeyvalues.h>
#include <entity2/entitysystem.h>
#include <variant.h>

namespace VoltMod::Sdk
{
using namespace VoltMod::Core;

namespace
{

// CS2's sound-event EmitSound_t; layout copied from CS2Fixes (src/voltmod_sdk/entity/
// globaltypes.h) - NOT the legacy Source1 struct in the SDK's shareddefs.h.
struct EmitSoundParams
{
    const char* SoundName = nullptr;       // 0x00
    Vector SoundOrigin{0.0f, 0.0f, 0.0f};  // 0x08
    float Volume = 1.0f;                   // 0x14
    float SoundTime = 0.0f;                // 0x18
    uint8_t Pad1C[0x4]{};                  // 0x1c
    uint32_t ForceGuid = 0;                // 0x20
    uint8_t Pad24[0x4]{};                  // 0x24
    int16_t Pitch = 100;                   // 0x28
    uint8_t Flags = 0;                     // 0x2a
};
// Passed by const reference, so only field offsets matter to the engine ABI.
static_assert(offsetof(EmitSoundParams, Volume) == 0x14);
static_assert(offsetof(EmitSoundParams, ForceGuid) == 0x20);
static_assert(offsetof(EmitSoundParams, Pitch) == 0x28);

// Returned by value from the EmitSoundFilter engine call; the exact size matters
// for the hidden sret ABI. Copied from CS2Fixes.
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

// Prototypes mirror CS2Fixes' src/addresses.h; re-verify there after CS2 updates.
using CreateEntityByNameFn = CEntityInstance* (*)(const char* className, int forceEdictIndex);
using DispatchSpawnFn = void (*)(CEntityInstance* entity, CEntityKeyValues* kv);
using AcceptInputFn = void (*)(CEntityInstance* self, const char* input, CEntityInstance* activator,
                               CEntityInstance* caller, variant_t* value, int outputId, void* unknown);
using AddEntityIOEventFn = void (*)(CEntitySystem* system, CEntityInstance* target, const char* input,
                                    CEntityInstance* activator, CEntityInstance* caller, variant_t* value,
                                    float delaySeconds, int outputId, void* unknown1, void* unknown2);
using UtilRemoveFn = void (*)(CEntityInstance* entity);
using SetModelFn = void (*)(CEntityInstance* entity, const char* modelPath);
using EmitSoundParamsFn = void (*)(CEntityInstance* entity, const char* soundEvent, int pitch, float volume,
                                   float delay);
using EmitSoundFilterFn = StartSoundEventInfo (*)(IRecipientFilter& filter, CEntityIndex sourceIndex,
                                                  const EmitSoundParams& params);

// Collapses the guard + dispatch the AcceptInput* wrappers all repeat; each only differs in how it
// builds the variant_t (kept out of the public header per the EntityOpsService members' note).
void FireInput(void* acceptInput, CEntityInstance* entity, const char* input, variant_t& value,
               CEntityInstance* activator, CEntityInstance* caller)
{
    if (!acceptInput || !entity || !input)
        return;
    std::bit_cast<AcceptInputFn>(acceptInput)(entity, input, activator, caller, &value, 0, nullptr);
}

}  // namespace

EntityOpsService::EntityOpsService(EntitySystem& entities, GameData& gameData, SchemaService& schema)
    : _entities(entities), _gameData(gameData), _schema(schema)
{}

bool EntityOpsService::Initialize()
{
    struct SignatureSlot
    {
        const char* Name;
        void** Slot;
    };

    const SignatureSlot signatures[] = {
        {"CreateEntityByName", &_createEntityByName},
        {"DispatchSpawn", &_dispatchSpawn},
        {"CEntityInstance_AcceptInput", &_acceptInput},
        {"CEntitySystem_AddEntityIOEvent", &_addEntityIOEvent},
        {"UTIL_Remove", &_utilRemove},
        {"CBaseModelEntity_SetModel", &_setModel},
        {"CBaseEntity_EmitSoundParams", &_emitSoundParams},
        {"CBaseEntity_EmitSoundFilter", &_emitSoundFilter},
    };

    for (const auto& signature : signatures)
    {
        *signature.Slot = _gameData.FindSignature(signature.Name);
        if (!*signature.Slot)
            Log::Warn("Entity ops: signature '{}' not resolved; the dependent operation is disabled.", signature.Name);
    }

    return CanSpawn();
}

bool EntityOpsService::CanSpawn() const
{
    return _createEntityByName && _dispatchSpawn && _acceptInput;
}

CEntityInstance* EntityOpsService::CreateByName(const char* className)
{
    if (!_createEntityByName || !className)
        return nullptr;

    return std::bit_cast<CreateEntityByNameFn>(_createEntityByName)(className, -1);
}

void EntityOpsService::DispatchSpawn(CEntityInstance* entity, EntityKeyValues* kv)
{
    if (!_dispatchSpawn || !entity)
        return;

    std::bit_cast<DispatchSpawnFn>(_dispatchSpawn)(entity, kv ? kv->Detach() : nullptr);
}

CEntityInstance* EntityOpsService::Spawn(const char* className, EntityKeyValues& kv)
{
    if (!CanSpawn())
        return nullptr;

    CEntityInstance* entity = CreateByName(className);
    if (!entity)
        return nullptr;

    DispatchSpawn(entity, &kv);
    return entity;
}

void EntityOpsService::AcceptInput(CEntityInstance* entity, const char* input, const char* param,
                                   CEntityInstance* activator, CEntityInstance* caller)
{
    variant_t value(param ? param : "");
    FireInput(_acceptInput, entity, input, value, activator, caller);
}

void EntityOpsService::AcceptInputFloat(CEntityInstance* entity, const char* input, float value,
                                        CEntityInstance* activator, CEntityInstance* caller)
{
    variant_t variant(value);
    FireInput(_acceptInput, entity, input, variant, activator, caller);
}

void EntityOpsService::AddIOEvent(CEntityInstance* target, const char* input, float delaySeconds,
                                  CEntityInstance* activator, CEntityInstance* caller)
{
    if (!_addEntityIOEvent || !target || !input)
        return;

    CEntitySystem* system = _entities.GetEntitySystem();
    if (!system)
        return;

    variant_t value("");
    std::bit_cast<AddEntityIOEventFn>(_addEntityIOEvent)(system, target, input, activator, caller, &value, delaySeconds,
                                                         0, nullptr, nullptr);
}

void EntityOpsService::Remove(CEntityInstance* entity)
{
    if (!_utilRemove || !entity)
        return;

    std::bit_cast<UtilRemoveFn>(_utilRemove)(entity);
}

void EntityOpsService::RemoveDelayed(CEntityInstance* entity, float delaySeconds)
{
    AddIOEvent(entity, "Kill", delaySeconds);
}

void EntityOpsService::SetModel(CEntityInstance* entity, const char* modelPath)
{
    if (!_setModel || !entity || !modelPath)
        return;

    std::bit_cast<SetModelFn>(_setModel)(entity, modelPath);
}

void EntityOpsService::EmitSound(CEntityInstance* entity, const char* soundEvent, int pitch, float volume, float delay)
{
    if (!_emitSoundParams || !entity || !soundEvent)
        return;

    std::bit_cast<EmitSoundParamsFn>(_emitSoundParams)(entity, soundEvent, pitch, volume, delay);
}

void EntityOpsService::EmitSoundFilter(IRecipientFilter& filter, CEntityInstance* source, const char* soundEvent,
                                       float volume, int pitch)
{
    if (!_emitSoundFilter || !source || !soundEvent)
        return;

    EmitSoundParams params;
    params.SoundName = soundEvent;
    params.Volume = volume;
    params.Pitch = static_cast<int16_t>(pitch);

    CEntityIndex sourceIndex(source->m_pEntity->m_EHandle.GetEntryIndex());
    std::bit_cast<EmitSoundFilterFn>(_emitSoundFilter)(filter, sourceIndex, params);
}

void EntityOpsService::NotifyFieldChanged(CEntityInstance* entity, const char* className, const char* fieldName)
{
    if (!entity || !className || !fieldName)
        return;

    int offset = _schema.GetOffset(className, fieldName);
    if (offset < 0)
        return;

    entity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(offset)));
}

}  // namespace VoltMod::Sdk
