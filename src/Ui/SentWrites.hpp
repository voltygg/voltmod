#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace VoltMod
{

/** What a write set, so a dialog variable and a class of the same name on the same panel are
 *  remembered apart instead of shadowing one another. */
enum class WriteKind
{
    Variable,
    Class
};

/**
 * @brief What each player was last sent for a layout, so an unchanged write is skipped.
 *
 * A menu is redrawn every tick, but unlike center HTML a networked layout does not need
 * re-sending to stay on screen: this is what turns that redraw into writes for the handful of
 * panels that actually changed. SDK-free so the rules are unit-tested; a @ref UiPanel owns one
 * and does the writing. Internal to `src/`: a panel is the whole public surface.
 *
 * Bound to @ref SlotEvents, a slot changing hands drops everything remembered about it - the new
 * occupant has been told nothing, whatever the last one saw.
 */
class SentWrites
{
public:
    /** Forget a slot when a player joins or leaves it. Safe to call again. */
    void BindReset(SlotEvents& slots) { _slots.BindReset(slots); }

    /** True when @p value differs from what @p slot (or @ref EveryoneSlot) was last sent for
     *  (@p kind, @p panelId, @p name), remembering it. @p kind keeps a dialog variable and a class
     *  of the same name apart. */
    bool Changed(int slot, WriteKind kind, std::string_view panelId, std::string_view name, std::string_view value);

    /** True when @p enabled differs from the input-capture state @p slot was last sent, remembering it. */
    bool CaptureChanged(int slot, bool enabled);

    /** True the first time it is asked for @p slot, so a failure that repeats every frame is
     *  logged once. Only a slot changing hands or @ref ForgetAll resets it. */
    bool IsFirstFailure(int slot);

    /** Drop the values remembered for @p slot, so the next write goes through whatever it is.
     *  Leaves the failure flag alone: what failed once this generation still fails. */
    void Forget(int slot);

    /** Drop everything, failure flags included. For a new entity, which has been told nothing. */
    void ForgetAll();

private:
    struct SlotState
    {
        std::unordered_map<std::string, std::string> Values;
        std::optional<bool> Capture;
        bool Failed = false;
    };

    /** @p slot's bucket, or the shared one; null for a slot that is neither. */
    SlotState* At(int slot);

    /** (kind, panelId, name) joined into one key, in a buffer reused across calls. */
    const std::string& KeyFor(WriteKind kind, std::string_view panelId, std::string_view name);

    std::string _scratch;
    PerSlot<SlotState> _slots;
    SlotState _shared;
};

}  // namespace VoltMod
