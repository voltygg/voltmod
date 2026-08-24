#pragma once

#include <VoltMod/Core/ScheduledEffect.hpp>
#include <array>
#include <functional>
#include <unordered_map>

namespace VoltMod::Core
{

/**
 * @brief How to apply and tear down one player effect, expressed as data.
 *
 * The manager builds a @ref ScheduledEffect from this: `OnTick` runs every `TickIntervalMs`
 * (omit for state-only effects), `DurationMs > 0` auto-expires the effect, and `OnStop` runs
 * exactly once when it ends for any reason - so it is the single place to undo `OnTick`'s state.
 */
struct EffectSpec
{
    int TickIntervalMs = 0;       /**< >0 => repeating body. */
    int DurationMs = 0;           /**< >0 => auto-expire after this long. */
    bool RoundScoped = false;     /**< Auto-cancel via @ref EffectManager::CancelRoundScoped. */
    bool SurvivesDeath = false;   /**< Skip the death sweep (@ref EffectManager::CancelPerLife). */
    std::function<void()> OnTick; /**< Repeating body; null for state-only effects. */
    std::function<void()> OnStop; /**< Undo/restore; runs exactly once on any end. */
};

/**
 * @brief Per-target effect bookkeeping. Each active effect owns one @ref ScheduledEffect, which
 * holds its tick timer, auto-expire timer, and the single onStop that undoes its state.
 */
struct ActiveEffect
{
    bool RoundScoped = false;
    bool SurvivesDeath = false;
    ScheduledEffect Fx;
};

/**
 * @brief Per-slot registry of toggleable/timed player effects, keyed by a plugin-defined
 * integer id (cast your effect enum). Owns each effect's @ref ScheduledEffect and its
 * re-apply/replace semantics.
 *
 * Deliberately plugin-owned rather than a framework service: onStop closures touch pawns and
 * timers, so the owning plugin must control when CancelAll runs relative to engine teardown.
 */
class EffectManager
{
public:
    static constexpr int MaxSlots = 64;

    explicit EffectManager(Scheduler& scheduler) : _scheduler(scheduler) {}

    bool IsActive(int slot, int effectId) const;

    /**
     * @brief Register a new effect for `slot`. If an effect of the same id is already active,
     * it is cancelled first (re-apply/replace semantics). The effect self-expires after
     * `spec.DurationMs` (if set), running `spec.OnStop`; the now-inactive slot entry is
     * reclaimed lazily on the next Apply/Cancel for that id.
     */
    void Apply(int slot, int effectId, EffectSpec spec);

    void Cancel(int slot, int effectId);
    void CancelAllForSlot(int slot);
    /** Death sweep: cancel every effect on @p slot except those flagged `SurvivesDeath`
     *  (session-long grants that outlive a single life). */
    void CancelPerLife(int slot);
    /** Cancel every active effect registered with `roundScoped == true`, on every slot. */
    void CancelRoundScoped();
    void CancelAll();

private:
    // Snapshot the ids to cancel before cancelling: Cancel runs onStop, which may re-enter the
    // slot map, so the map must not be iterated while entries are erased.
    void CancelWhere(int slot, const std::function<bool(int id, const ActiveEffect&)>& keep);

    Scheduler& _scheduler;
    // Slot-indexed array of small maps: only a handful of effects run per player at once.
    std::array<std::unordered_map<int, ActiveEffect>, MaxSlots> _effects{};
};

}  // namespace VoltMod::Core
