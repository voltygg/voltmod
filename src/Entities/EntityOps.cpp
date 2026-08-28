#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <algorithm>
#include <bit>
#include <entity2/entityinstance.h>
#include <entity2/entitykeyvalues.h>
#include <entity2/entitysystem.h>
#include <string>
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

// The public header cannot expose the SDK's variant_t. The engine resolves the input by name
// during the call and does not keep the pointer, so a NUL-terminated temporary is enough.
static void FireInput(const Bindings& bindings, CEntityInstance* entity, std::string_view input, variant_t& value,
                      CEntityInstance* activator, CEntityInstance* caller)
{
    if (!bindings.AcceptInput || !entity || input.empty())
        return;
    bindings.AcceptInput(entity, std::string(input).c_str(), activator, caller, &value, 0, nullptr);
}

bool EntityOps::CanSpawn() const
{
    return static_cast<bool>(_bindings.CreateEntityByName) && static_cast<bool>(_bindings.DispatchSpawn);
}

CEntityInstance* EntityOps::CreateByName(std::string_view className)
{
    if (!_bindings.CreateEntityByName || className.empty())
        return nullptr;

    return _bindings.CreateEntityByName(std::string(className).c_str(), -1);
}

void EntityOps::DispatchSpawn(CEntityInstance* entity, KeyValues* kv)
{
    if (!_bindings.DispatchSpawn || !entity)
        return;

    _bindings.DispatchSpawn(entity, kv ? kv->Detach() : nullptr);
}

CEntityInstance* EntityOps::Spawn(std::string_view className, KeyValues& kv)
{
    if (!CanSpawn())
        return nullptr;

    CEntityInstance* entity = CreateByName(className);
    if (!entity)
        return nullptr;

    DispatchSpawn(entity, &kv);
    return entity;
}

void EntityOps::AcceptInput(CEntityInstance* entity, std::string_view input, std::string_view param,
                            CEntityInstance* activator, CEntityInstance* caller)
{
    // variant_t holds the pointer rather than copying, so the buffer has to outlive the call below.
    const std::string text(param);
    variant_t value(text.c_str());
    FireInput(_bindings, entity, input, value, activator, caller);
}

void EntityOps::AcceptInputFloat(CEntityInstance* entity, std::string_view input, float value,
                                 CEntityInstance* activator, CEntityInstance* caller)
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

void EntityOps::AddIOEvent(CEntityInstance* target, std::string_view input, float delaySeconds,
                           CEntityInstance* activator, CEntityInstance* caller)
{
    if (!_bindings.AddEntityIOEvent || !target || input.empty())
        return;

    CEntitySystem* system = _entities.GetEntitySystem();
    if (!system)
        return;

    // The queue copies the input name when it takes the event, so this temporary is enough.
    const std::string name(input);
    variant_t value("");
    _bindings.AddEntityIOEvent(system, target, name.c_str(), activator, caller, &value, delaySeconds, 0, nullptr,
                               nullptr);
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

void EntityOps::SetModel(CEntityInstance* entity, std::string_view modelPath)
{
    if (!_bindings.SetModel || !entity || modelPath.empty())
        return;

    _bindings.SetModel(entity, std::string(modelPath).c_str());
}

void EntityOps::EmitSound(CEntityInstance* entity, std::string_view soundEvent, int pitch, float volume, float delay)
{
    if (!_bindings.EmitSoundParams || !entity || soundEvent.empty())
        return;

    _bindings.EmitSoundParams(entity, std::string(soundEvent).c_str(), pitch, volume, delay);
}

void EntityOps::EmitSoundFilter(IRecipientFilter& filter, CEntityInstance* source, std::string_view soundEvent,
                                float volume, int pitch)
{
    if (!_bindings.EmitSoundFilter || !source || soundEvent.empty())
        return;

    // EmitSoundParams borrows the name; the engine reads it during the call below.
    const std::string sound(soundEvent);
    EmitSoundParams params;
    params.SoundName = sound.c_str();
    params.Volume = volume;
    params.Pitch = static_cast<int16_t>(pitch);

    CEntityIndex sourceIndex(source->m_pEntity->m_EHandle.GetEntryIndex());
    std::bit_cast<EmitSoundFilterFn>(_bindings.EmitSoundFilter.Ptr())(filter, sourceIndex, params);
}

}  // namespace VoltMod
