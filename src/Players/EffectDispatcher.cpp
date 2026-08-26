#include <VoltMod/Players/EffectDispatcher.hpp>
#include <utility>

namespace VoltMod
{

void EffectDispatcher::Apply(int adminSlot, int targetSlot, const EffectDescriptor& effect, int param) const
{
    auto ctx = _actions.Resolve(adminSlot, targetSlot, effect.Permission);
    if (!ctx)
        return;
    if (effect.RequireAlive && !ctx->TargetPawn().IsAlive())
        return;
    if (!effect.Setup)
        return;
    if (effect.Choices && (param < 0 || param >= static_cast<int>(effect.Choices().size())))
        return;

    EffectInstance inst = effect.Setup(*ctx, param);
    // Register only when there is state to track: a pure fire-and-forget never occupies the slot
    // map, so IsActive stays false and no stale toggle state lingers.
    if (inst.OnTick || inst.OnStop || effect.DurationMs > 0)
        _effects.Apply(targetSlot, effect.Id, std::move(inst), effect.Scope, effect.TickIntervalMs, effect.DurationMs);

    if (!effect.OnKey.empty())
        _actions.Broadcast(*ctx, effect.OnKey);
}

void EffectDispatcher::Clear(int adminSlot, int targetSlot, const EffectDescriptor& effect) const
{
    auto ctx = _actions.Resolve(adminSlot, targetSlot, effect.Permission);
    if (!ctx || !_effects.IsActive(targetSlot, effect.Id))
        return;

    _effects.Cancel(targetSlot, effect.Id);
    if (!effect.OffKey.empty())
        _actions.Broadcast(*ctx, effect.OffKey);
}

void EffectDispatcher::Toggle(int adminSlot, int targetSlot, const EffectDescriptor& effect, int param) const
{
    if (_effects.IsActive(targetSlot, effect.Id))
        Clear(adminSlot, targetSlot, effect);
    else
        Apply(adminSlot, targetSlot, effect, param);
}

}  // namespace VoltMod
