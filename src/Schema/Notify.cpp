#include <VoltMod/Engine/Memory/MemoryAccess.hpp>
#include <VoltMod/Schema/Notify.hpp>
#include <entity2/entityinstance.h>

namespace VoltMod::Schema
{

// The engine's CNetworkVarChainer: the link from a replicated sub-object to its owner. Only the
// two fields the notification needs are named; the padding is the engine's layout, re-verify
// after a CS2 update.
struct OwnerLink
{
    CEntityInstance* Entity;
    uint8_t Pad[24];
    ChangeAccessorFieldPathIndex_t PathIndex;
};
static_assert(offsetof(OwnerLink, PathIndex) == 0x20);

void NotifyEntity(CEntityInstance* entity, int32_t offset)
{
    if (!entity)
        return;

    entity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(offset)));
}

void NotifyComponentOwner(void* component, int32_t ownerLinkOffset, int32_t offset)
{
    if (!component || ownerLinkOffset < 0)
        return;

    auto* link = MemberPtr<OwnerLink>(component, ownerLinkOffset);
    if (!link->Entity)
        return;

    link->Entity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(offset), -1, link->PathIndex));
}

CEntityInstance* ComponentOwner(const void* component, int32_t ownerLinkOffset)
{
    if (!component || ownerLinkOffset < 0)
        return nullptr;
    // MemberPtr takes a mutable base; the read here never writes through it.
    return MemberPtr<const OwnerLink>(const_cast<void*>(component), ownerLinkOffset)->Entity;
}

}  // namespace VoltMod::Schema
