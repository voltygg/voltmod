#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/NetChannel.hpp>
#include <inetchannelinfo.h>
#include <string>

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

std::string_view NetChannels::GetUserInfoCvar(int slot, std::string_view name) const
{
    auto* engine = IsValidSlot(slot) ? _interfaces.Engine : nullptr;
    if (!engine || name.empty())
        return {};

    const char* value = engine->GetClientConVarValue(CPlayerSlot(slot), std::string(name).c_str());
    return value ? std::string_view(value) : std::string_view{};
}

}  // namespace VoltMod
