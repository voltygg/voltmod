#include "Engine/NetMessage.hpp"

#include <VoltMod/Engine/Interfaces.hpp>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string>

namespace VoltMod
{

bool PostUserMessage(Interfaces& interfaces, INetworkMessageInternal*& cached, std::string_view partialName,
                     IRecipientFilter& filter, const std::function<bool(CNetMessage*)>& fill)
{
    if (!interfaces.GameEventSystem || !interfaces.NetworkMessages)
        return false;

    if (!cached)
        cached = interfaces.NetworkMessages->FindNetworkMessagePartial(std::string(partialName).c_str());
    if (!cached)
        return false;

    CNetMessage* message = cached->AllocateMessage();
    if (!message)
        return false;

    bool posted = false;
    if (fill(message))
    {
        interfaces.GameEventSystem->PostEventAbstract(-1, false, &filter, cached, message, 0);
        posted = true;
    }

    interfaces.NetworkMessages->DeallocateNetMessageAbstract(cached, message);
    return posted;
}

}  // namespace VoltMod
