#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Entities/Field.hpp>
#include <VoltMod/Entities/Render.hpp>
#include <entity2/entityinstance.h>

namespace VoltMod
{

void SetRender(CEntityInstance* entity, RenderMode_t mode, uint32_t color)
{
    static const LazyField renderMode{"CBaseModelEntity", "m_nRenderMode", sizeof(uint8_t)};
    static const LazyField renderColor{"CBaseModelEntity", "m_clrRender", sizeof(uint32_t)};

    if (!entity || !renderMode || !renderColor)
        return;

    WriteAt<uint8_t>(entity, renderMode->Offset, static_cast<uint8_t>(mode));
    WriteAt<uint32_t>(entity, renderColor->Offset, color);

    // Raw writes don't dirty the network state, so without these the new values only replicate
    // when something else touches the entity that tick.
    MarkChanged(entity, *renderMode);
    MarkChanged(entity, *renderColor);
}

}  // namespace VoltMod
