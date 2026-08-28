#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace VoltMod
{

/** Which write namespace a key belongs to, so a dialog variable and a CSS class of the same
 *  name on the same panel are tracked separately instead of shadowing one another. */
enum class UiProperty
{
    Text,
    Class
};

/**
 * @brief What each player has already been told about a layout, so an unchanged frame is free.
 *
 * A menu is redrawn every tick, but unlike center HTML a networked layout does not need
 * re-sending to stay on screen: this is what turns that redraw into writes for the handful of
 * panels that actually changed. SDK-free so the dedupe rules are unit-tested; @ref UiLayout owns
 * one and does the writing.
 *
 * Bound to @ref SlotEvents, a slot changing hands drops everything remembered about it - the new
 * occupant has been told nothing, whatever the last one saw.
 */
class UiWriteCache
{
public:
    /** Reset a slot's memory when a player joins or leaves it. Idempotent. */
    void Bind(SlotEvents& slots) { _slots.BindReset(slots); }

    /** Record @p value under (@p kind, @p panelId, @p name) for @p slot; true when it is new or
     *  different. @p kind keeps a dialog variable and a CSS class of the same name apart. */
    bool Update(int slot, UiProperty kind, std::string_view panelId, std::string_view name,
                std::string_view value);

    /** Record @p enabled as @p slot's input-capture state; true when it changed. */
    bool UpdateCapture(int slot, bool enabled);

    /** True the first time it is asked for @p slot, so a failure that repeats every frame is
     *  logged once. Only a slot changing hands or @ref ForgetAll re-arms it. */
    bool FirstFailure(int slot);

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

    /** (kind, panelId, name) joined into one key, in a buffer reused across calls. */
    const std::string& Key(UiProperty kind, std::string_view panelId, std::string_view name);

    std::string _scratch;
    PerSlot<SlotState> _slots;
};

}  // namespace VoltMod
