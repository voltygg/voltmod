#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Hooks/InputHistory.hpp>
#include <VoltMod/Hooks/Movement.hpp>
#include <algorithm>
#include <cassert>

namespace VoltMod
{

void InputHistory::Enable(int depth)
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

    // Subscribing is also what installs the movement hook, so nothing else has to arm it.
    if (!_cmdListener)
        _cmdListener = _movement.PreCmd += [this](int slot, const UserCmdView& cmd) { Record(slot, cmd); };
    if (!_slotListener)
        _slotListener = _slots.Changed += [this](int slot) { Clear(slot); };
}

void InputHistory::Record(int slot, const UserCmdView& cmd)
{
    if (!IsValidSlot(slot) || !cmd.Valid)
        return;

    Ring& ring = _rings[slot];
    ring.Samples[ring.Head] = cmd;
    ring.Head = (ring.Head + 1) % _depth;
    ring.Count = std::min(ring.Count + 1, _depth);
}

int InputHistory::Count(int slot) const
{
    return IsValidSlot(slot) ? _rings[slot].Count : 0;
}

const UserCmdView& InputHistory::At(int slot, int ago) const
{
    assert(IsValidSlot(slot));
    const Ring& ring = _rings[slot];
    assert(ago >= 0 && ago < ring.Count);
    int index = (ring.Head - 1 - ago + 2 * _depth) % _depth;
    return ring.Samples[index];
}

void InputHistory::Clear(int slot)
{
    if (!IsValidSlot(slot))
        return;
    _rings[slot].Head = 0;
    _rings[slot].Count = 0;
}

void InputHistory::ClearAll()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
        Clear(slot);
}

}  // namespace VoltMod
