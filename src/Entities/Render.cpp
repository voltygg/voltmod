#include <VoltMod/Entities/Render.hpp>
#include <VoltMod/Entities/SchemaPtr.hpp>
#include <entity2/entityinstance.h>

namespace VoltMod
{

void SetRender(CEntityInstance* entity, RenderMode_t mode, uint32_t color)
{
    static const FieldOffset renderMode{"CBaseModelEntity", "m_nRenderMode", sizeof(uint8_t)};
    static const FieldOffset renderColor{"CBaseModelEntity", "m_clrRender", sizeof(uint32_t)};

    // Both or neither: a half-applied render state is worse than an unchanged one.
    const SchemaPtr target{entity};
    if (!target || !renderMode || !renderColor)
        return;

    target.Set<uint8_t>(renderMode, static_cast<uint8_t>(mode));
    target.Set<uint32_t>(renderColor, color);

    // Raw writes don't dirty the network state, so without these the new values only replicate
    // when something else touches the entity that tick.
    MarkChanged(entity, *renderMode);
    MarkChanged(entity, *renderColor);
}

}  // namespace VoltMod
