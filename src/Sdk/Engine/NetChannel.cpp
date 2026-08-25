#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Sdk/Engine/GameInterfaces.hpp>
#include <VoltMod/Sdk/Engine/NetChannel.hpp>
#include <inetchannelinfo.h>

namespace VoltMod::Sdk
{

INetChannelInfo* NetChannelService::GetNetInfo(int slot) const
{
    auto* engine = Core::IsValidSlot(slot) ? _interfaces.Engine : nullptr;
    return engine ? engine->GetPlayerNetInfo(CPlayerSlot(slot)) : nullptr;
}

float NetChannelService::EngineLatency(int slot) const
{
    auto* info = GetNetInfo(slot);
    return info ? info->GetEngineLatency() : 0.0f;
}

const char* NetChannelService::GetUserInfoCvar(int slot, const char* name) const
{
    auto* engine = Core::IsValidSlot(slot) ? _interfaces.Engine : nullptr;
    if (!engine || !name)
        return nullptr;
    return engine->GetClientConVarValue(CPlayerSlot(slot), name);
}

}  // namespace VoltMod::Sdk
