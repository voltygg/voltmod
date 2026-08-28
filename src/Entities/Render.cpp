#include <VoltMod/Entities/Render.hpp>
#include <VoltMod/Entities/SchemaPtr.hpp>
#include <entity2/entityinstance.h>

namespace VoltMod
{

void SetRender(CEntityInstance* entity, RenderMode_t mode, uint32_t color)
{
    static const SchemaField<uint8_t> renderMode{"CBaseModelEntity", "m_nRenderMode"};
    static const SchemaField<uint32_t> renderColor{"CBaseModelEntity", "m_clrRender"};

    // Both or neither: a half-applied render state is worse than an unchanged one.
    const SchemaPtr target{entity};
    if (!target || !renderMode || !renderColor)
        return;

    target.Set(renderMode, static_cast<uint8_t>(mode));
    target.Set(renderColor, color);

    // Raw writes don't dirty the network state, so without these the new values only replicate
    // when something else touches the entity that tick.
    MarkChanged(entity, *renderMode);
    MarkChanged(entity, *renderColor);
}

}  // namespace VoltMod
