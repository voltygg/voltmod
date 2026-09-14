#include "Engine/Net/ServerSideClients.hpp"

#include <VoltMod/Engine/Memory/MemoryAccess.hpp>
#include <cstdint>
#include <iserver.h>
#include <tier1/utlvector.h>

namespace VoltMod
{

using ClientVector = CUtlVector<void*>;

/** The slot-indexed client vector, or nullptr when the server or the offset is unavailable. */
static ClientVector* Clients(const Interfaces& interfaces, const Bindings& bindings)
{
    if (!interfaces.NetworkServerService || !bindings.ServerClients)
        return nullptr;

    // Null between map loads, which is ordinary rather than an error.
    void* server = interfaces.NetworkServerService->GetIGameServer();
    if (!server)
        return nullptr;

    return MemberPtr<ClientVector>(server, bindings.ServerClients.Value());
}

void* AnyClient(const Interfaces& interfaces, const Bindings& bindings)
{
    ClientVector* clients = Clients(interfaces, bindings);
    if (!clients)
        return nullptr;

    for (int i = 0; i < clients->Count(); ++i)
        if (void* client = clients->Element(i))
            return client;
    return nullptr;
}

int SlotOfClient(const Bindings& bindings, const void* client)
{
    if (!client || !bindings.ClientSlot)
        return -1;
    return bindings.ClientSlot.Read(client);
}

}  // namespace VoltMod
