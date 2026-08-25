#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
#include <utility>

namespace VoltMod::Players
{

namespace
{

// Build the EffectSpec from the descriptor's declarative lifetime plus the body's instance.
Core::EffectSpec MakeSpec(EffectScope scope, int tickIntervalMs, int durationMs, EffectInstance inst)
{
    return {.TickIntervalMs = tickIntervalMs,
            .DurationMs = durationMs,
            .RoundScoped = scope == EffectScope::Round,
            .SurvivesDeath = scope == EffectScope::Session,
            .OnTick = std::move(inst.OnTick),
            .OnStop = std::move(inst.OnStop)};
}

// Shared body for the Clear verbs (both key off Permission/Id/OffKey only).
void ClearById(Runtime& runtime, Core::EffectManager& effects, int adminSlot, int targetSlot,
               const std::string& permission, int id, const std::string& offKey)
{
    ActionDispatcher dispatch{runtime};
    auto ctx = dispatch.Resolve(adminSlot, targetSlot, permission);
    if (!ctx.Valid() || !effects.IsActive(targetSlot, id))
        return;

    effects.Cancel(targetSlot, id);
    if (!offKey.empty())
        dispatch.Broadcast(ctx, offKey);
}

void BroadcastKey(const Players::ActionContext& ctx, const std::string& key)
{
    if (!key.empty())
        ActionDispatcher{ctx.Rt}.Broadcast(ctx, key);
}

}  // namespace

void EffectDispatcher::Apply(int adminSlot, int targetSlot, const EffectDescriptor& effect) const
{
    auto ctx = ActionDispatcher{_runtime}.Resolve(adminSlot, targetSlot, effect.Permission);
    if (!ctx.Valid())
        return;
    if (effect.RequireAlive && !ctx.TargetCtrl.IsAlive())
        return;

    EffectInstance inst = effect.Setup ? effect.Setup(ctx) : EffectInstance{};
    // Register only when there is state to track: a pure fire-and-forget never occupies the slot
    // map, so IsActive stays false and no stale toggle state lingers.
    if (inst.OnTick || inst.OnStop || effect.DurationMs > 0)
        _effects.Apply(targetSlot, effect.Id,
                       MakeSpec(effect.Scope, effect.TickIntervalMs, effect.DurationMs, std::move(inst)));

    BroadcastKey(ctx, effect.OnKey);
}

void EffectDispatcher::Clear(int adminSlot, int targetSlot, const EffectDescriptor& effect) const
{
    ClearById(_runtime, _effects, adminSlot, targetSlot, effect.Permission, effect.Id, effect.OffKey);
}

void EffectDispatcher::Toggle(int adminSlot, int targetSlot, const EffectDescriptor& effect) const
{
    if (_effects.IsActive(targetSlot, effect.Id))
        Clear(adminSlot, targetSlot, effect);
    else
        Apply(adminSlot, targetSlot, effect);
}

void EffectDispatcher::Apply(int adminSlot, int targetSlot, int param, const ParamEffectDescriptor& effect) const
{
    auto ctx = ActionDispatcher{_runtime}.Resolve(adminSlot, targetSlot, effect.Permission);
    if (!ctx.Valid() || !effect.Setup)
        return;
    if (effect.RequireAlive && !ctx.TargetCtrl.IsAlive())
        return;
    if (param < 0 || (effect.Choices && param >= static_cast<int>(effect.Choices().size())))
        return;

    EffectInstance inst = effect.Setup(ctx, param);
    _effects.Apply(targetSlot, effect.Id,
                   MakeSpec(effect.Scope, effect.TickIntervalMs, effect.DurationMs, std::move(inst)));
    BroadcastKey(ctx, effect.OnKey);
}

void EffectDispatcher::Clear(int adminSlot, int targetSlot, const ParamEffectDescriptor& effect) const
{
    ClearById(_runtime, _effects, adminSlot, targetSlot, effect.Permission, effect.Id, effect.OffKey);
}

void ToggleEffect(Runtime& runtime, Core::EffectManager& effects, int adminSlot, int targetSlot,
                  const EffectDescriptor& effect)
{
    EffectDispatcher{runtime, effects}.Toggle(adminSlot, targetSlot, effect);
}

void ApplyEffect(Runtime& runtime, Core::EffectManager& effects, int adminSlot, int targetSlot,
                 const EffectDescriptor& effect)
{
    EffectDispatcher{runtime, effects}.Apply(adminSlot, targetSlot, effect);
}

void ClearEffect(Runtime& runtime, Core::EffectManager& effects, int adminSlot, int targetSlot,
                 const EffectDescriptor& effect)
{
    EffectDispatcher{runtime, effects}.Clear(adminSlot, targetSlot, effect);
}

void ApplyEffect(Runtime& runtime, Core::EffectManager& effects, int adminSlot, int targetSlot, int param,
                 const ParamEffectDescriptor& effect)
{
    EffectDispatcher{runtime, effects}.Apply(adminSlot, targetSlot, param, effect);
}

void ClearEffect(Runtime& runtime, Core::EffectManager& effects, int adminSlot, int targetSlot,
                 const ParamEffectDescriptor& effect)
{
    EffectDispatcher{runtime, effects}.Clear(adminSlot, targetSlot, effect);
}

}  // namespace VoltMod::Players
