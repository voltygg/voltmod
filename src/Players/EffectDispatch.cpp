#include <CS2Kit/Core/EffectManager.hpp>
#include <CS2Kit/Players/EffectDescriptor.hpp>
#include <utility>

namespace CS2Kit::Players
{

using Players::ActionDispatcher;

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
void ClearById(Core::EffectManager& effects, int adminSlot, int targetSlot, const std::string& permission, int id,
               const std::string& offKey)
{
    ActionDispatcher dispatch;
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
        ActionDispatcher{}.Broadcast(ctx, key);
}

}  // namespace

void ApplyEffect(Core::EffectManager& effects, int adminSlot, int targetSlot, const EffectDescriptor& effect)
{
    auto ctx = ActionDispatcher{}.Resolve(adminSlot, targetSlot, effect.Permission);
    if (!ctx.Valid())
        return;
    if (effect.RequireAlive && !ctx.TargetCtrl.IsAlive())
        return;

    EffectInstance inst = effect.Setup ? effect.Setup(ctx) : EffectInstance{};
    // Register only when there is state to track: a pure fire-and-forget never occupies the slot
    // map, so IsActive stays false and no stale toggle state lingers.
    if (inst.OnTick || inst.OnStop || effect.DurationMs > 0)
        effects.Apply(targetSlot, effect.Id,
                      MakeSpec(effect.Scope, effect.TickIntervalMs, effect.DurationMs, std::move(inst)));

    BroadcastKey(ctx, effect.OnKey);
}

void ClearEffect(Core::EffectManager& effects, int adminSlot, int targetSlot, const EffectDescriptor& effect)
{
    ClearById(effects, adminSlot, targetSlot, effect.Permission, effect.Id, effect.OffKey);
}

void ToggleEffect(Core::EffectManager& effects, int adminSlot, int targetSlot, const EffectDescriptor& effect)
{
    if (effects.IsActive(targetSlot, effect.Id))
        ClearEffect(effects, adminSlot, targetSlot, effect);
    else
        ApplyEffect(effects, adminSlot, targetSlot, effect);
}

void ApplyEffect(Core::EffectManager& effects, int adminSlot, int targetSlot, int param,
                 const ParamEffectDescriptor& effect)
{
    auto ctx = ActionDispatcher{}.Resolve(adminSlot, targetSlot, effect.Permission);
    if (!ctx.Valid() || !effect.Setup)
        return;
    if (effect.RequireAlive && !ctx.TargetCtrl.IsAlive())
        return;
    if (param < 0 || (effect.Choices && param >= static_cast<int>(effect.Choices().size())))
        return;

    EffectInstance inst = effect.Setup(ctx, param);
    effects.Apply(targetSlot, effect.Id,
                  MakeSpec(effect.Scope, effect.TickIntervalMs, effect.DurationMs, std::move(inst)));
    BroadcastKey(ctx, effect.OnKey);
}

void ClearEffect(Core::EffectManager& effects, int adminSlot, int targetSlot, const ParamEffectDescriptor& effect)
{
    ClearById(effects, adminSlot, targetSlot, effect.Permission, effect.Id, effect.OffKey);
}

}  // namespace CS2Kit::Players
