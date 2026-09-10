#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Menu/MenuFreeze.hpp>

namespace VoltMod
{

MenuFreeze::MenuFreeze(EntitySystem& entities, Scheduler& scheduler, SlotEvents& slots)
    : _entities(entities), _scheduler(scheduler)
{
    _states.BindReset(slots);
}

void MenuFreeze::Enable(bool enabled)
{
    _enabled = enabled;

    // Turning it off releases whoever the previous setting already froze; leaving them stuck
    // until they close a menu they may not know is open is not a defensible reading of "off".
    // Turning it on catches up the sessions that were already open.
    if (!enabled)
    {
        for (int slot = 0; slot < MaxPlayers; ++slot)
            _states[slot].Movement.Release(_entities.PawnOf(slot));
    }

    SyncFrameWork();
}

void MenuFreeze::Open(int slot, bool requested)
{
    if (!IsValidSlot(slot))
        return;

    _states[slot].Requested = requested;
    if (_enabled && requested)
        _states[slot].Movement.Hold(_entities.PawnOf(slot));

    SyncFrameWork();
}

void MenuFreeze::Close(int slot)
{
    if (!IsValidSlot(slot))
        return;

    // Releasing is never gated on the setting: a hold taken while it was on must come back.
    _states[slot].Requested = false;
    _states[slot].Movement.Release(_entities.PawnOf(slot));

    SyncFrameWork();
}

void MenuFreeze::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_states[slot].Requested)
            _states[slot].Movement.Sync(_entities.PawnOf(slot));
    }
}

void MenuFreeze::SyncFrameWork()
{
    bool wanted = false;
    if (_enabled)
    {
        for (int slot = 0; slot < MaxPlayers && !wanted; ++slot)
            wanted = _states[slot].Requested;
    }

    if (!wanted)
        _onFrame.Reset();
    else if (!_onFrame)
        _onFrame = _scheduler.EveryFrame([this] { OnGameFrame(); });
}

}  // namespace VoltMod
