#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Schema/Generated/CBeam.hpp>
#include <string>

namespace VoltMod
{

// The engine's SolidType_t values the `solid` key takes.
static constexpr int SolidNone = 0;
static constexpr int SolidVPhysics = 6;

Status EntitySystem::Available() const
{
    if (!_interfaces.GameResourceService)
    {
        return std::unexpected(Error::NotReady("IGameResourceService not available"));
    }
    if (!_bindings.GameEntitySystem)
    {
        return std::unexpected(Error::Unsupported("the GameEntitySystem offset did not bind"));
    }
    if (!_bindings.CreateEntityByName || !_bindings.DispatchSpawn)
    {
        return std::unexpected(Error::Unsupported("CreateEntityByName or DispatchSpawn did not bind"));
    }
    return {};
}

Entity EntitySystem::Create(std::string_view className)
{
    // Checked here so every create-then-spawn path gets it.
    if (!Available() || className.empty())
    {
        return {};
    }
    return {*this, _bindings.CreateEntityByName(std::string(className).c_str(), -1)};
}

Entity EntitySystem::Spawn(std::string_view className, KeyValues& values)
{
    Entity entity = Create(className);
    entity.Spawn(values);
    return entity;
}

Entity EntitySystem::SpawnProp(const PropSpec& prop)
{
    KeyValues values;
    values.Set("model", prop.Model)
        .Set("origin", prop.Origin)
        .Set("angles", prop.Angles)
        .Set("solid", prop.Solid ? SolidVPhysics : SolidNone);
    if (!prop.Skin.empty())
    {
        values.Set("skin", prop.Skin);
    }
    if (!prop.CastsShadow)
    {
        values.Set("disableshadows", 1);
    }

    Entity entity = Create("prop_dynamic");
    // The physics shapes take their owner as they are created.
    if (prop.Owner)
    {
        entity.SetOwnerRef(prop.Owner);
    }
    entity.Spawn(values);
    // "solid" 0 alone leaves the model's hull blocking traces.
    if (entity && !prop.Solid)
    {
        entity.AcceptInput("DisableCollision");
    }
    if (entity && prop.Scale != 1.0f)
    {
        entity.SetScale(prop.Scale);
    }
    return entity;
}

Entity EntitySystem::SpawnParticle(std::string_view effect, const Vector& origin, const QAngle& angles)
{
    if (effect.empty())
    {
        return {};
    }
    KeyValues values;
    values.Set("effect_name", effect).Set("start_active", 1).Set("origin", origin).Set("angles", angles);
    return Spawn("info_particle_system", values);
}

Entity EntitySystem::SpawnBeam(const Vector& from, const Vector& to, float width, Color color)
{
    // Not env_beam: a spawned env_beam brings the server down within seconds.
    Entity line = Create("beam");
    const Schema::CBeam beam{line.Raw()};
    if (!beam)
    {
        return {};
    }
    beam.SetWidth(width);
    beam.SetEndWidth(width);
    beam.SetEndPos(to);
    line.SetRender(Schema::RenderMode_t::kRenderNormal, color);

    KeyValues values;
    values.Set("origin", from);
    line.Spawn(values);
    return line;
}

}  // namespace VoltMod
