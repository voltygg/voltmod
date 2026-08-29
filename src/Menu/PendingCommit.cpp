#include "Menu/PendingCommit.hpp"

#include <VoltMod/Core/Slot.hpp>
#include <utility>

namespace VoltMod
{

PendingCommit::PendingCommit(Timer timer) : _timer(std::move(timer)) {}

void PendingCommit::BindReset(SlotEvents& slots)
{
    // Resetting an entry drops its Subscription, which is what cancels the timer: the player the
    // value was picked for is gone, so applying it now would act for whoever took the slot.
    _entries.BindReset(slots);
}

void PendingCommit::Arm(int slot, int index, std::function<void()> commit)
{
    if (!IsValidSlot(slot) || !commit)
        return;

    // A different row means the cursor moved on without the value being applied; run it here
    // rather than losing it. The same row is a burst, and only restarts the delay.
    if (_entries[slot].Index != index)
        Run(slot);

    Entry& entry = _entries[slot];
    entry.Index = index;
    entry.Commit = std::move(commit);
    entry.Timer = _timer ? _timer(DelayMs, [this, slot] { Run(slot); }) : Subscription{};
}

int PendingCommit::Index(int slot) const
{
    return IsValidSlot(slot) ? _entries[slot].Index : -1;
}

bool PendingCommit::IsPending(int slot, int index) const
{
    return index >= 0 && Index(slot) == index;
}

void PendingCommit::Run(int slot)
{
    if (!IsValidSlot(slot))
        return;

    // Taken out of the slot before it runs: the commit may open a menu, close one, or arm the
    // next value, and none of that may find the entry it is replacing still here. Dropping
    // `taken` at the end of the scope cancels the timer, including when this *is* that timer.
    Entry taken = std::move(_entries[slot]);
    _entries[slot] = Entry{};

    if (taken.Commit)
        taken.Commit();
}

void PendingCommit::RunAll()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
        Run(slot);
}

void PendingCommit::Cancel(int slot)
{
    if (IsValidSlot(slot))
        _entries[slot] = Entry{};
}

}  // namespace VoltMod
