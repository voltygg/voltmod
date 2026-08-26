#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Players/EffectDispatcher.hpp>
#include <VoltMod/Runtime.hpp>
#include <utility>

namespace VoltMod
{

// Build the EffectSpec from the descriptor's declarative lifetime plus the body's instance.
static EffectSpec MakeSpec(EffectScope scope, int tickIntervalMs, int durationMs, EffectInstance inst)
{
    return {.TickIntervalMs = tickIntervalMs,
            .DurationMs = durationMs,
            .RoundScoped = scope == EffectScope::Round,
            .SurvivesDeath = scope == EffectScope::Session,
            .OnTick = std::move(inst.OnTick),
            .OnStop = std::move(inst.OnStop)};
}

void EffectDispatcher::ClearById(int adminSlot, int targetSlot, const std::string& permission, int id,
                                 const std::string& offKey) const
{
    auto ctx = _actions.Resolve(adminSlot, targetSlot, permission);
    if (!ctx || !_effects.IsActive(targetSlot, id))
        return;

    _effects.Cancel(targetSlot, id);
    if (!offKey.empty())
        BroadcastKey(*ctx, offKey);
}

void EffectDispatcher::BroadcastKey(const ActionContext& ctx, const std::string& key) const
{
    if (key.empty())
        return;

    if (auto& sink = _runtime.Policy.Broadcast)
        sink(ctx.Auth, key);
}

void EffectDispatcher::Apply(int adminSlot, int targetSlot, const EffectDescriptor& effect) const
{
    auto ctx = _actions.Resolve(adminSlot, targetSlot, effect.Permission);
    if (!ctx)
        return;
    if (effect.RequireAlive && !ctx->TargetPawn().IsAlive())
        return;

    EffectInstance inst = effect.Setup ? effect.Setup(*ctx) : EffectInstance{};
    // Register only when there is state to track: a pure fire-and-forget never occupies the slot
    // map, so IsActive stays false and no stale toggle state lingers.
    if (inst.OnTick || inst.OnStop || effect.DurationMs > 0)
        _effects.Apply(targetSlot, effect.Id,
                       MakeSpec(effect.Scope, effect.TickIntervalMs, effect.DurationMs, std::move(inst)));

    BroadcastKey(*ctx, effect.OnKey);
}

void EffectDispatcher::Clear(int adminSlot, int targetSlot, const EffectDescriptor& effect) const
{
    ClearById(adminSlot, targetSlot, effect.Permission, effect.Id, effect.OffKey);
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
    auto ctx = _actions.Resolve(adminSlot, targetSlot, effect.Permission);
    if (!ctx || !effect.Setup)
        return;
    if (effect.RequireAlive && !ctx->TargetPawn().IsAlive())
        return;
    if (param < 0 || (effect.Choices && param >= static_cast<int>(effect.Choices().size())))
        return;

    EffectInstance inst = effect.Setup(*ctx, param);
    _effects.Apply(targetSlot, effect.Id,
                   MakeSpec(effect.Scope, effect.TickIntervalMs, effect.DurationMs, std::move(inst)));
    BroadcastKey(*ctx, effect.OnKey);
}

void EffectDispatcher::Clear(int adminSlot, int targetSlot, const ParamEffectDescriptor& effect) const
{
    ClearById(adminSlot, targetSlot, effect.Permission, effect.Id, effect.OffKey);
}

}  // namespace VoltMod
