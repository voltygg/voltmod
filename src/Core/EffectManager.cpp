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
    // Self-expired effects remain until reclaimed; stopped timers are inactive.
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
        // Reapplying the same id stops it before this callback erases the entry; references to
        // existing unordered_map elements remain valid when other entries grow the map.
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

    // Detach before stopping so re-entrant Apply sees a clean slot. Stop runs OnStop once.
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
