#include <CS2Kit/Core/Slot.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/InputHistoryService.hpp>
#include <algorithm>
#include <cassert>

namespace CS2Kit::Sdk
{

InputHistoryService::~InputHistoryService()
{
    // _slots is declared ahead of this service in Services, so it outlives us either way.
    if (_slotListener != 0)
        _slots.Remove(_slotListener);
    // MovementHook still has to go through the hub, which may already be gone on shutdown.
    if (auto* services = CS2Kit::Detail::RtOrNull(); services && _cmdListener != 0)
        services->MovementHook.RemoveListener(_cmdListener);
}

void InputHistoryService::Enable(int depth)
{
    depth = std::max(depth, 1);
    if (depth > _depth)
    {
        _depth = depth;
        for (auto& ring : _rings)
        {
            ring.Samples.assign(_depth, {});
            ring.Head = 0;
            ring.Count = 0;
        }
    }

    if (_cmdListener == 0)
        _cmdListener = CS2Kit::Detail::Rt().MovementHook.ListenPreCmd(
            [this](int slot, const UserCmdView& cmd) { Record(slot, cmd); });
    if (_slotListener == 0)
        _slotListener = _slots.Listen([this](int slot) { Clear(slot); });
}

void InputHistoryService::Record(int slot, const UserCmdView& cmd)
{
    if (!Core::IsValidSlot(slot) || !cmd.Valid)
        return;

    Ring& ring = _rings[slot];
    ring.Samples[ring.Head] = cmd;
    ring.Head = (ring.Head + 1) % _depth;
    ring.Count = std::min(ring.Count + 1, _depth);
}

int InputHistoryService::Count(int slot) const
{
    return Core::IsValidSlot(slot) ? _rings[slot].Count : 0;
}

const UserCmdView& InputHistoryService::At(int slot, int ago) const
{
    const Ring& ring = _rings[slot];
    assert(ago >= 0 && ago < ring.Count);
    int index = (ring.Head - 1 - ago + 2 * _depth) % _depth;
    return ring.Samples[index];
}

void InputHistoryService::Clear(int slot)
{
    if (!Core::IsValidSlot(slot))
        return;
    _rings[slot].Head = 0;
    _rings[slot].Count = 0;
}

void InputHistoryService::ClearAll()
{
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
        Clear(slot);
}

}  // namespace CS2Kit::Sdk
