#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/NetChannel.hpp>
#include <inetchannelinfo.h>

namespace VoltMod
{

INetChannelInfo* NetChannels::GetNetInfo(int slot) const
{
    auto* engine = IsValidSlot(slot) ? _interfaces.Engine : nullptr;
    return engine ? engine->GetPlayerNetInfo(CPlayerSlot(slot)) : nullptr;
}

float NetChannels::EngineLatency(int slot) const
{
    auto* info = GetNetInfo(slot);
    return info ? info->GetEngineLatency() : 0.0f;
}

const char* NetChannels::GetUserInfoCvar(int slot, const char* name) const
{
    auto* engine = IsValidSlot(slot) ? _interfaces.Engine : nullptr;
    if (!engine || !name)
        return nullptr;
    return engine->GetClientConVarValue(CPlayerSlot(slot), name);
}

}  // namespace VoltMod
