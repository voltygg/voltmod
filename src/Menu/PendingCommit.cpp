#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Menu/PendingCommit.hpp>
#include <utility>

namespace VoltMod
{

PendingCommit::PendingCommit(Timer timer) : _timer(std::move(timer)) {}

void PendingCommit::BindReset(SlotEvents& slots)
{
    // Dropping the subscription drops a commit when the slot changes hands.
    _entries.BindReset(slots);
}

void PendingCommit::Hold(int slot, int index, std::function<void()> commit)
{
    if (!IsValidSlot(slot) || !commit)
        return;

    // Commit the previous row before holding a different one. Holding the same row again only
    // restarts its delay.
    if (_entries[slot].Index != index)
        Apply(slot);

    Entry& entry = _entries[slot];
    entry.Index = index;
    entry.Commit = std::move(commit);
    entry.Timer = _timer ? _timer(DelayMs, [this, slot] { Apply(slot); }) : Subscription{};
}

int PendingCommit::Index(int slot) const
{
    return IsValidSlot(slot) ? _entries[slot].Index : -1;
}

bool PendingCommit::IsPending(int slot, int index) const
{
    return index >= 0 && Index(slot) == index;
}

void PendingCommit::Apply(int slot)
{
    if (!IsValidSlot(slot))
        return;

    // Remove the entry before running it. The callback may replace it or change the menu.
    Entry taken = std::move(_entries[slot]);
    _entries[slot] = Entry{};

    if (taken.Commit)
        taken.Commit();
}

void PendingCommit::Drop(int slot)
{
    if (IsValidSlot(slot))
        _entries[slot] = Entry{};
}

}  // namespace VoltMod
