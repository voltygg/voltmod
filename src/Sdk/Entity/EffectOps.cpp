#include "Sdk/Internal/Schema.hpp"

#include <Color.h>
#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Sdk/Engine/MemoryAccess.hpp>
#include <VoltMod/Sdk/Entity/EffectOps.hpp>
#include <VoltMod/Sdk/Entity/EntityKeyValues.hpp>
#include <VoltMod/Sdk/Entity/EntityOps.hpp>
#include <mathlib/vector.h>

namespace VoltMod::Sdk::EffectOps
{

CEntityInstance* SpawnParticle(const char* effectName, const Vector& origin, float lifetimeSeconds)
{
    auto& ops = VoltMod::Detail::Rt().EntityOps;
    if (!ops.CanSpawn() || !effectName)
        return nullptr;

    CEntityInstance* particle = ops.CreateByName("info_particle_system");
    if (!particle)
        return nullptr;

    EntityKeyValues kv;
    kv.Set("effect_name", effectName).Set("origin", origin).Set("start_active", true);
    ops.DispatchSpawn(particle, &kv);

    if (lifetimeSeconds > 0.0f)
    {
        // DestroyImmediately stops the effect; the trailing Kill frees the entity.
        ops.AddIOEvent(particle, "DestroyImmediately", lifetimeSeconds);
        ops.AddIOEvent(particle, "Kill", lifetimeSeconds + 0.02f);
    }

    return particle;
}

CEntityInstance* SpawnBeam(const Vector& from, const Vector& to, const Color& color, float width, float lifetimeSeconds)
{
    auto& ops = VoltMod::Detail::Rt().EntityOps;
    if (!ops.CanSpawn())
        return nullptr;

    CEntityInstance* beam = ops.CreateByName("beam");
    if (!beam)
        return nullptr;

    // Endpoint/width/color live in schema fields with no spawn keyvalue; written
    // before DispatchSpawn they go out with the first network snapshot.
    auto& schema = VoltMod::Detail::Rt().Schema();
    int offsetWidth = schema.GetOffsetOf<float>("CBeam", "m_fWidth");
    int offsetEndPos = schema.GetOffsetOf<Vector>("CBeam", "m_vecEndPos");
    int offsetColor = schema.GetOffsetOf<Color>("CBaseModelEntity", "m_clrRender");

    if (offsetWidth >= 0)
        WriteAt<float>(beam, offsetWidth, width);
    if (offsetEndPos >= 0)
        WriteAt<Vector>(beam, offsetEndPos, to);
    if (offsetColor >= 0)
        WriteAt<Color>(beam, offsetColor, color);

    EntityKeyValues kv;
    kv.Set("origin", from);
    ops.DispatchSpawn(beam, &kv);

    if (lifetimeSeconds > 0.0f)
        ops.RemoveDelayed(beam, lifetimeSeconds);

    return beam;
}

CEntityInstance* SpawnProp(const char* modelPath, const Vector& origin, bool physics, float lifetimeSeconds)
{
    auto& ops = VoltMod::Detail::Rt().EntityOps;
    if (!ops.CanSpawn() || !modelPath)
        return nullptr;

    // The _override variants skip the prop-data validation that rejects most
    // models on the plain prop_physics/prop_dynamic classes.
    CEntityInstance* prop = ops.CreateByName(physics ? "prop_physics_override" : "prop_dynamic_override");
    if (!prop)
        return nullptr;

    EntityKeyValues kv;
    kv.Set("origin", origin);
    ops.DispatchSpawn(prop, &kv);
    ops.SetModel(prop, modelPath);

    if (lifetimeSeconds > 0.0f)
        ops.RemoveDelayed(prop, lifetimeSeconds);

    return prop;
}

}  // namespace VoltMod::Sdk::EffectOps
