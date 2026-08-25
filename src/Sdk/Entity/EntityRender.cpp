#include "Sdk/Internal/Schema.hpp"

#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Sdk/Engine/MemoryAccess.hpp>
#include <VoltMod/Sdk/Entity/EntityRender.hpp>
#include <entity2/entityinstance.h>

namespace VoltMod::Sdk
{

void SetEntityRender(CEntityInstance* entity, RenderMode_t mode, uint32_t color)
{
    if (!entity)
        return;

    // m_nRenderMode/m_clrRender offsets are fixed by the loaded game binary, so resolve them
    // once (this runs every disco tick). Cached only on success so an early failed lookup retries.
    static int modeOffset = -1;
    static int colorOffset = -1;
    if (modeOffset < 0 || colorOffset < 0)
    {
        auto& schema = VoltMod::Detail::Rt().Schema();
        modeOffset = schema.GetOffsetOf<uint8_t>("CBaseModelEntity", "m_nRenderMode");
        colorOffset = schema.GetOffsetOf<uint32_t>("CBaseModelEntity", "m_clrRender");
        if (modeOffset < 0 || colorOffset < 0)
            return;
    }

    WriteAt<uint8_t>(entity, modeOffset, static_cast<uint8_t>(mode));
    WriteAt<uint32_t>(entity, colorOffset, color);

    // Raw writes don't dirty the network state, so without these the new values
    // only replicate when something else touches the entity that tick.
    entity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(modeOffset)));
    entity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(colorOffset)));
}

}  // namespace VoltMod::Sdk
