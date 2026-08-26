#include <VoltMod/Core/EffectManager.hpp>
#include <utility>
#include <vector>

namespace VoltMod
{

bool EffectManager::IsActive(int slot, int effectId) const
{
    if (!IsValidSlot(slot))
        return false;
    auto it = _effects[slot].find(effectId);
    // A self-expired effect (durationMs elapsed) leaves its entry behind until reclaimed; its
    // timers report stopped, so treat that as not-active without touching the map.
    return it != _effects[slot].end() && !it->second.Stopped;
}

void EffectManager::Apply(int slot, int effectId, EffectInstance instance, EffectScope scope, int tickIntervalMs,
                          int durationMs)
{
    if (!IsValidSlot(slot))
        return;

    Cancel(slot, effectId);  // re-apply semantics: replace any active instance

    auto [it, inserted] =
        _effects[slot].insert_or_assign(effectId, ActiveEffect{.Scope = scope, .OnStop = std::move(instance.OnStop)});
    ActiveEffect& active = it->second;

    if (instance.OnTick && tickIntervalMs > 0)
        active.Tick = _scheduler.Repeat(tickIntervalMs, std::move(instance.OnTick));

    if (durationMs > 0)
    {
        // The map may grow (another Apply for a different id) between now and the timer firing,
        // which can reallocate the unordered_map's buckets - but never invalidates a reference to
        // an existing element, so capturing `&active` across that is safe. A subsequent Cancel for
        // this same id (from re-apply or a plugin call) runs Stop() and the erase below only
        // happens after that, so this timer never fires against a stale, already-stopped entry.
        active.Expiry = _scheduler.Delay(durationMs, [&active]() { active.Stop(); });
    }
}

void EffectManager::Cancel(int slot, int effectId)
{
    if (!IsValidSlot(slot))
        return;
    auto it = _effects[slot].find(effectId);
    if (it == _effects[slot].end())
        return;

    // Detach before stopping so a re-entrant Apply sees a clean slot. Stop() runs OnStop once
    // (a no-op if the effect already self-expired).
    ActiveEffect entry = std::move(it->second);
    _effects[slot].erase(it);
    entry.Stop();
}

void EffectManager::CancelWhere(int slot, const std::function<bool(int id, const ActiveEffect&)>& keep)
{
    if (!IsValidSlot(slot))
        return;

    std::vector<int> ids;
    ids.reserve(_effects[slot].size());
    for (const auto& [id, entry] : _effects[slot])
        if (keep(id, entry))
            ids.push_back(id);
    for (int id : ids)
        Cancel(slot, id);
}

void EffectManager::CancelAll(int slot)
{
    if (!IsValidSlot(slot))
        return;
    CancelWhere(slot, [](int, const ActiveEffect&) { return true; });
}

void EffectManager::CancelOnDeath(int slot)
{
    if (!IsValidSlot(slot))
        return;
    CancelWhere(slot, [](int, const ActiveEffect& e) { return e.Scope != EffectScope::Session; });
}

void EffectManager::CancelRound()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
        CancelWhere(slot, [](int, const ActiveEffect& e) { return e.Scope == EffectScope::Round; });
}

void EffectManager::CancelAll()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
        CancelAll(slot);
}

}  // namespace VoltMod
