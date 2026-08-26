#include "Engine/NetMessage.hpp"

#include <VoltMod/Engine/Interfaces.hpp>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>

namespace VoltMod
{

bool PostUserMessage(Interfaces& interfaces, INetworkMessageInternal*& cached, const char* partialName,
                     IRecipientFilter& filter, const std::function<bool(CNetMessage*)>& fill)
{
    if (!interfaces.GameEventSystem || !interfaces.NetworkMessages)
        return false;

    if (!cached)
        cached = interfaces.NetworkMessages->FindNetworkMessagePartial(partialName);
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
