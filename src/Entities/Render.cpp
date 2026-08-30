#include <VoltMod/Entities/Render.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <entity2/entityinstance.h>

namespace VoltMod
{

void SetRender(CEntityInstance* entity, RenderMode_t mode, uint32_t color)
{
    // Both or neither: a half-applied render state is worse than an unchanged one. The generated
    // setters dirty the entity themselves, so the new values replicate on the next update.
    const Schema::CBaseModelEntity target{entity};
    if (!target)
        return;

    target.SetRenderMode(static_cast<Schema::RenderMode_t>(mode));
    target.SetRenderColor(color);
}

}  // namespace VoltMod
