#pragma once

#include <CS2Kit/Core/Slot.hpp>
#include <CS2Kit/Core/SlotEvents.hpp>
#include <CS2Kit/Core/Subscription.hpp>
#include <CS2Kit/Sdk/UserCmd.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace CS2Kit::Sdk
{

/**
 * @brief Opt-in per-slot ring buffer of recent decoded usercmds.
 *
 * Dormant until Enable(depth): it then records every UserCmdView the
 * MovementHook decodes (ListenPreCmd) and clears a slot's history when its
 * player joins or leaves. The MovementHook itself must still be installed by
 * some plugin (the usual lazy PlayerSpawn pattern) for samples to flow.
 *
 * Lookback is index-based: At(slot, 0) is the newest command, At(slot, 1) the
 * one before it, up to Count(slot)-1. Invalid views (Valid=false) are not
 * recorded.
 */
class InputHistoryService
{
public:
    /** @p slots tells this service when a slot changes hands, without it needing the roster. */
    explicit InputHistoryService(Core::SlotEvents& slots) : _slots(slots) {}

    InputHistoryService(const InputHistoryService&) = delete;
    InputHistoryService& operator=(const InputHistoryService&) = delete;

    /** Start recording with @p depth samples kept per player. Idempotent; depth grows only. */
    void Enable(int depth = 128);
    bool Enabled() const { return _depth > 0; }
    int Depth() const { return _depth; }

    /** Number of samples currently buffered for @p slot (0 when disabled/invalid). */
    int Count(int slot) const;

    /** The @p ago-th newest sample for @p slot; ago must be < Count(slot). */
    const UserCmdView& At(int slot, int ago) const;

    void Clear(int slot);
    void ClearAll();

private:
    struct Ring
    {
        std::vector<UserCmdView> Samples;  // sized to _depth once enabled
        int Head = 0;                      // next write position
        int Count = 0;
    };

    void Record(int slot, const UserCmdView& cmd);

    Core::SlotEvents& _slots;
    std::array<Ring, Core::MaxPlayers> _rings{};
    int _depth = 0;
    Core::Subscription _cmdListener;
    Core::Subscription _slotListener;
};

}  // namespace CS2Kit::Sdk
