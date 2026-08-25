#include "Sdk/Internal/Schema.hpp"

#include <VoltMod/Sdk/Engine/MemoryAccess.hpp>
#include <VoltMod/Sdk/Entity/EntityRender.hpp>
#include <entity2/entityinstance.h>

namespace VoltMod::Sdk
{

void SetEntityRender(SchemaService& schema, CEntityInstance* entity, RenderMode_t mode, uint32_t color)
{
    if (!entity)
        return;

    // SchemaService caches both lookups, so this stays a map hit per disco tick.
    const int modeOffset = schema.GetOffsetOf<uint8_t>("CBaseModelEntity", "m_nRenderMode");
    const int colorOffset = schema.GetOffsetOf<uint32_t>("CBaseModelEntity", "m_clrRender");
    if (modeOffset < 0 || colorOffset < 0)
        return;

    WriteAt<uint8_t>(entity, modeOffset, static_cast<uint8_t>(mode));
    WriteAt<uint32_t>(entity, colorOffset, color);

    // Raw writes don't dirty the network state, so without these the new values
    // only replicate when something else touches the entity that tick.
    entity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(modeOffset)));
    entity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(colorOffset)));
}

}  // namespace VoltMod::Sdk
