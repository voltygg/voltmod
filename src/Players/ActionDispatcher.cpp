#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/PlayerManager.hpp>

namespace VoltMod
{

Result<ActionContext> ActionDispatcher::Resolve(PlayerRef caller, PlayerRef target, std::string_view permission) const
{
    // The refs go through untouched: Authorize is what rejects one whose slot has changed hands.
    auto authorized = _policy.Authorize(caller, target, permission);
    if (!authorized)
        return std::unexpected(authorized.error());

    // Safe only now - Authorize confirmed both refs still name the players they were taken for.
    return ActionContext{.Auth = *authorized,
                         .CallerCtrl = _entities.Controller(caller.Slot),
                         .TargetCtrl = _entities.Controller(target.Slot)};
}

void ActionDispatcher::Run(PlayerRef caller, PlayerRef target, const Action& action) const
{
    auto ctx = Resolve(caller, target, action.Permission);
    if (!ctx)
        return;
    if (action.RequireAlive && !ctx->TargetPawn().IsAlive())
        return;
    if (auto key = action.Body(*ctx))
        Broadcast(*ctx, *key);
}

void ActionDispatcher::Run(PlayerRef caller, PlayerRef target, int param, const ParamAction& action) const
{
    auto ctx = Resolve(caller, target, action.Permission);
    if (!ctx)
        return;
    if (action.RequireAlive && !ctx->TargetPawn().IsAlive())
        return;
    if (auto key = action.Body(*ctx, param))
        Broadcast(*ctx, *key);
}

void ActionDispatcher::Broadcast(const ActionContext& ctx, std::string_view translationKey) const
{
    if (auto& callback = _policy.Broadcast)
        callback(ctx.Auth, translationKey);
}

}  // namespace VoltMod
