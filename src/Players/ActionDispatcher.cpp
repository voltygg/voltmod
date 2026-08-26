#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/PlayerManager.hpp>

namespace VoltMod
{

Result<ActionContext> ActionDispatcher::Resolve(int callerSlot, int targetSlot, std::string_view permission) const
{
    auto authorized = _policy.Authorize(_players.RefFor(callerSlot), _players.RefFor(targetSlot), permission);
    if (!authorized)
        return std::unexpected(authorized.error());

    return ActionContext{.Auth = *authorized,
                         .CallerCtrl = _entities.Controller(callerSlot),
                         .TargetCtrl = _entities.Controller(targetSlot)};
}

void ActionDispatcher::Run(int callerSlot, int targetSlot, const Action& action) const
{
    auto ctx = Resolve(callerSlot, targetSlot, action.Permission);
    if (!ctx)
        return;
    if (action.RequireAlive && !ctx->TargetPawn().IsAlive())
        return;
    if (auto key = action.Body(*ctx))
        Broadcast(*ctx, *key);
}

void ActionDispatcher::Run(int callerSlot, int targetSlot, int param, const ParamAction& action) const
{
    auto ctx = Resolve(callerSlot, targetSlot, action.Permission);
    if (!ctx)
        return;
    if (action.RequireAlive && !ctx->TargetPawn().IsAlive())
        return;
    if (auto key = action.Body(*ctx, param))
        Broadcast(*ctx, *key);
}

void ActionDispatcher::Broadcast(const ActionContext& ctx, std::string_view translationKey) const
{
    if (auto& sink = _policy.Broadcast)
        sink(ctx.Auth, translationKey);
}

}  // namespace VoltMod
