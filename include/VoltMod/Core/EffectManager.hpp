#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <array>
#include <functional>
#include <unordered_map>

namespace VoltMod
{

/** Lifetime policy for a player effect, declared as data rather than baked into the body. */
enum class EffectScope
{
    Persistent, /**< Lives until toggled off, death, disconnect, or unload. */
    Round,      /**< Also auto-cancels on round end/prestart (@ref EffectManager::CancelRound). */
    Session     /**< Survives death; cleared only on toggle-off, disconnect, or unload. */
};

/**
 * @brief What an effect's setup body hands back: the two closures @ref EffectManager drives.
 * `OnTick` runs every `tickIntervalMs` (null for state-only effects); `OnStop` undoes whatever
 * was applied and runs exactly once when the effect ends for any reason.
 */
struct EffectInstance
{
    std::function<void()> OnTick;
    std::function<void()> OnStop;
};

/**
 * @brief One active effect's bookkeeping: the tick and auto-expiry timers, and the single
 * `OnStop` that undoes it. Owns its `Subscription`s directly - dropping either cancels the
 * corresponding timer, and `OnStop` runs exactly once, whichever timer (or an explicit Cancel)
 * triggers it first.
 */
struct ActiveEffect
{
    EffectScope Scope = EffectScope::Persistent;
    std::function<void()> OnStop;
    Subscription Tick;
    Subscription Expiry;
    bool Stopped = false;

    /** Run `OnStop` (once) and drop both timers. Safe to call repeatedly. */
    void Stop()
    {
        if (Stopped)
            return;
        Stopped = true;
        Tick.Reset();
        Expiry.Reset();
        if (OnStop)
        {
            auto cb = std::move(OnStop);
            OnStop = nullptr;
            cb();
        }
    }
};

/**
 * @brief Per-slot registry of toggleable/timed player effects, keyed by a plugin-defined
 * integer id (cast your effect enum). Owns each effect's timers and its re-apply/replace
 * semantics.
 *
 * Deliberately plugin-owned rather than a framework service: `OnStop` closures touch pawns and
 * timers, so the owning plugin must control when CancelAll runs relative to engine teardown.
 */
class EffectManager
{
public:
    explicit EffectManager(Scheduler& scheduler) : _scheduler(scheduler) {}

    bool IsActive(int slot, int effectId) const;

    /**
     * @brief Register a new effect for `slot`. If an effect of the same id is already active,
     * it is cancelled first (re-apply/replace semantics). `instance.OnTick` runs every
     * `tickIntervalMs` (skip for state-only effects); `durationMs > 0` auto-expires the effect,
     * running `instance.OnStop`. The now-inactive slot entry is reclaimed lazily on the next
     * Apply/Cancel for that id.
     */
    void Apply(int slot, int effectId, EffectInstance instance, EffectScope scope, int tickIntervalMs, int durationMs);

    void Cancel(int slot, int effectId);
    /** Cancel every active effect on @p slot. */
    void CancelAll(int slot);
    /** Cancel every active effect, on every slot. */
    void CancelAll();
    /** Death sweep: cancel every effect on @p slot except those scoped `Session` (grants that
     *  outlive a single life). */
    void CancelOnDeath(int slot);
    /** Cancel every active effect scoped `Round`, on every slot. */
    void CancelRound();

private:
    // Snapshot the ids to cancel before cancelling: Cancel runs OnStop, which may re-enter the
    // slot map, so the map must not be iterated while entries are erased.
    void CancelWhere(int slot, const std::function<bool(int id, const ActiveEffect&)>& keep);

    Scheduler& _scheduler;
    // Slot-indexed array of small maps: only a handful of effects run per player at once.
    std::array<std::unordered_map<int, ActiveEffect>, MaxPlayers> _effects{};
};

}  // namespace VoltMod
