#include <CS2Kit/Core/Slot.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <CS2Kit/Sdk/NetChannel.hpp>
#include <CS2Kit/Sdk/SdkServices.hpp>
#include <inetchannelinfo.h>

namespace CS2Kit::Sdk
{

namespace
{

IVEngineServer2* EngineServer(int slot)
{
    if (!Core::IsValidSlot(slot))
        return nullptr;
    auto* services = CtxOrNull();
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

}  // namespace CS2Kit::Sdk
