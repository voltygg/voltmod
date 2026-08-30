#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Schema/Notify.hpp>
#include <entity2/entityinstance.h>

namespace VoltMod::Schema
{

// The chain object every replicated sub-object hangs off. Only the two fields the notification
// needs are named; the padding is the engine's layout, re-verify after a CS2 update.
struct NetworkVarChainer
{
    CEntityInstance* Entity;
    uint8_t Pad[24];
    ChangeAccessorFieldPathIndex_t PathIndex;
};
static_assert(offsetof(NetworkVarChainer, PathIndex) == 0x20);

void NotifyEntity(CEntityInstance* entity, int32_t offset)
{
    if (!entity)
        return;

    entity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(offset)));
}

void NotifyThroughChain(void* component, int32_t chainOffset, int32_t offset)
{
    if (!component || chainOffset < 0)
        return;

    auto* chainer = MemberPtr<NetworkVarChainer>(component, chainOffset);
    if (!chainer->Entity)
        return;

    chainer->Entity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(offset), -1, chainer->PathIndex));
}

}  // namespace VoltMod::Schema
