#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Sdk/Engine/GameInterfaces.hpp>
#include <VoltMod/Sdk/Engine/NetChannel.hpp>
#include <inetchannelinfo.h>

namespace VoltMod::Sdk
{

namespace
{

IVEngineServer2* EngineServer(int slot)
{
    if (!Core::IsValidSlot(slot))
        return nullptr;
    auto* services = VoltMod::Detail::RtOrNull();
    return services ? services->Interfaces.Engine : nullptr;
}

}  // namespace

INetChannelInfo* NetChannelService::GetNetInfo(int slot) const
{
    auto* engine = EngineServer(slot);
    return engine ? engine->GetPlayerNetInfo(CPlayerSlot(slot)) : nullptr;
}

float NetChannelService::EngineLatency(int slot) const
{
    auto* info = GetNetInfo(slot);
    return info ? info->GetEngineLatency() : 0.0f;
}

const char* NetChannelService::GetUserInfoCvar(int slot, const char* name) const
{
    auto* engine = EngineServer(slot);
    if (!engine || !name)
        return nullptr;
    return engine->GetClientConVarValue(CPlayerSlot(slot), name);
}

}  // namespace VoltMod::Sdk
