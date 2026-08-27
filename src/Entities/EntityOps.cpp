#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <algorithm>
#include <bit>
#include <entity2/entityinstance.h>
#include <entity2/entitykeyvalues.h>
#include <entity2/entitysystem.h>
#include <variant.h>

namespace VoltMod
{

// CS2 EmitSound_t layout from CS2Fixes, not the legacy Source 1 SDK type.
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
// The engine receives this by reference, so field offsets define the ABI.
static_assert(offsetof(EmitSoundParams, Volume) == 0x14);
static_assert(offsetof(EmitSoundParams, ForceGuid) == 0x20);
static_assert(offsetof(EmitSoundParams, Pitch) == 0x28);

// EmitSoundFilter returns this through the hidden sret ABI.
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

// Keep this sret prototype aligned with CS2Fixes after game updates.
using EmitSoundFilterFn = StartSoundEventInfo (*)(IRecipientFilter& filter, CEntityIndex sourceIndex,
                                                  const EmitSoundParams& params);

// The public header cannot expose the SDK's variant_t.
static void FireInput(const Bindings& bindings, CEntityInstance* entity, const char* input, variant_t& value,
                      CEntityInstance* activator, CEntityInstance* caller)
{
    if (!bindings.AcceptInput || !entity || !input)
        return;
    bindings.AcceptInput(entity, input, activator, caller, &value, 0, nullptr);
}

bool EntityOps::CanSpawn() const
{
    return static_cast<bool>(_bindings.CreateEntityByName) && static_cast<bool>(_bindings.DispatchSpawn);
}

CEntityInstance* EntityOps::CreateByName(const char* className)
{
    if (!_bindings.CreateEntityByName || !className)
        return nullptr;

    return _bindings.CreateEntityByName(className, -1);
}

void EntityOps::DispatchSpawn(CEntityInstance* entity, KeyValues* kv)
{
    if (!_bindings.DispatchSpawn || !entity)
        return;

    _bindings.DispatchSpawn(entity, kv ? kv->Detach() : nullptr);
}

CEntityInstance* EntityOps::Spawn(const char* className, KeyValues& kv)
{
    if (!CanSpawn())
        return nullptr;

    CEntityInstance* entity = CreateByName(className);
    if (!entity)
        return nullptr;

    DispatchSpawn(entity, &kv);
    return entity;
}

void EntityOps::AcceptInput(CEntityInstance* entity, const char* input, const char* param, CEntityInstance* activator,
                            CEntityInstance* caller)
{
    variant_t value(param ? param : "");
    FireInput(_bindings, entity, input, value, activator, caller);
}

void EntityOps::AcceptInputFloat(CEntityInstance* entity, const char* input, float value, CEntityInstance* activator,
                                 CEntityInstance* caller)
{
    variant_t variant(value);
    FireInput(_bindings, entity, input, variant, activator, caller);
}

void EntityOps::SetModelScale(CEntityInstance* entity, float scale)
{
    // Extreme scales can destabilize collision and crash the server.
    constexpr float MinSafeModelScale = 0.05f;
    constexpr float MaxSafeModelScale = 3.0f;
    AcceptInputFloat(entity, "SetScale", std::clamp(scale, MinSafeModelScale, MaxSafeModelScale));
}

void EntityOps::AddIOEvent(CEntityInstance* target, const char* input, float delaySeconds, CEntityInstance* activator,
                           CEntityInstance* caller)
{
    if (!_bindings.AddEntityIOEvent || !target || !input)
        return;

    CEntitySystem* system = _entities.GetEntitySystem();
    if (!system)
        return;

    variant_t value("");
    _bindings.AddEntityIOEvent(system, target, input, activator, caller, &value, delaySeconds, 0, nullptr, nullptr);
}

void EntityOps::Remove(CEntityInstance* entity)
{
    if (!_bindings.UtilRemove || !entity)
        return;

    _bindings.UtilRemove(entity);
}

void EntityOps::RemoveDelayed(CEntityInstance* entity, float delaySeconds)
{
    AddIOEvent(entity, "Kill", delaySeconds);
}

void EntityOps::SetModel(CEntityInstance* entity, const char* modelPath)
{
    if (!_bindings.SetModel || !entity || !modelPath)
        return;

    _bindings.SetModel(entity, modelPath);
}

void EntityOps::EmitSound(CEntityInstance* entity, const char* soundEvent, int pitch, float volume, float delay)
{
    if (!_bindings.EmitSoundParams || !entity || !soundEvent)
        return;

    _bindings.EmitSoundParams(entity, soundEvent, pitch, volume, delay);
}

void EntityOps::EmitSoundFilter(IRecipientFilter& filter, CEntityInstance* source, const char* soundEvent, float volume,
                                int pitch)
{
    if (!_bindings.EmitSoundFilter || !source || !soundEvent)
        return;

    EmitSoundParams params;
    params.SoundName = soundEvent;
    params.Volume = volume;
    params.Pitch = static_cast<int16_t>(pitch);

    CEntityIndex sourceIndex(source->m_pEntity->m_EHandle.GetEntryIndex());
    std::bit_cast<EmitSoundFilterFn>(_bindings.EmitSoundFilter.Ptr())(filter, sourceIndex, params);
}

}  // namespace VoltMod
