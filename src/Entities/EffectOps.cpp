#include "Entities/Schema.hpp"

#include <Color.h>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Entities/EffectOps.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <mathlib/vector.h>

namespace VoltMod::EffectOps
{

CEntityInstance* SpawnParticle(EntityOps& ops, const char* effectName, const Vector& origin, float lifetimeSeconds)
{
    if (!ops.CanSpawn() || !effectName)
        return nullptr;

    CEntityInstance* particle = ops.CreateByName("info_particle_system");
    if (!particle)
        return nullptr;

    KeyValues kv;
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

CEntityInstance* SpawnBeam(EntityOps& ops, const Vector& from, const Vector& to, const Color& color, float width,
                           float lifetimeSeconds)
{
    if (!ops.CanSpawn())
        return nullptr;

    CEntityInstance* beam = ops.CreateByName("beam");
    if (!beam)
        return nullptr;

    // Endpoint/width/color live in schema fields with no spawn keyvalue; written
    // before DispatchSpawn they go out with the first network snapshot.
    auto& schema = ops.Schema();
    int offsetWidth = schema.GetOffsetOf<float>("CBeam", "m_fWidth");
    int offsetEndPos = schema.GetOffsetOf<Vector>("CBeam", "m_vecEndPos");
    int offsetColor = schema.GetOffsetOf<Color>("CBaseModelEntity", "m_clrRender");

    if (offsetWidth >= 0)
        WriteAt<float>(beam, offsetWidth, width);
    if (offsetEndPos >= 0)
        WriteAt<Vector>(beam, offsetEndPos, to);
    if (offsetColor >= 0)
        WriteAt<Color>(beam, offsetColor, color);

    KeyValues kv;
    kv.Set("origin", from);
    ops.DispatchSpawn(beam, &kv);

    if (lifetimeSeconds > 0.0f)
        ops.RemoveDelayed(beam, lifetimeSeconds);

    return beam;
}

CEntityInstance* SpawnProp(EntityOps& ops, const char* modelPath, const Vector& origin, bool physics,
                           float lifetimeSeconds)
{
    if (!ops.CanSpawn() || !modelPath)
        return nullptr;

    // The _override variants skip the prop-data validation that rejects most
    // models on the plain prop_physics/prop_dynamic classes.
    CEntityInstance* prop = ops.CreateByName(physics ? "prop_physics_override" : "prop_dynamic_override");
    if (!prop)
        return nullptr;

    KeyValues kv;
    kv.Set("origin", origin);
    ops.DispatchSpawn(prop, &kv);
    ops.SetModel(prop, modelPath);

    if (lifetimeSeconds > 0.0f)
        ops.RemoveDelayed(prop, lifetimeSeconds);

    return prop;
}

}  // namespace VoltMod::EffectOps
