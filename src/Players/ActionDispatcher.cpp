#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>

namespace VoltMod
{

Result<ActionContext> ActionDispatcher::Resolve(int callerSlot, int targetSlot, std::string_view permission) const
{
    auto& players = _runtime.Players;
    auto authorized = _runtime.Policy.Authorize(players.RefFor(callerSlot), players.RefFor(targetSlot), permission);
    if (!authorized)
        return std::unexpected(authorized.error());

    return ActionContext{.Auth = *authorized,
                         .Rt = _runtime,
                         .CallerCtrl = _runtime.Entities.Controller(callerSlot),
                         .TargetCtrl = _runtime.Entities.Controller(targetSlot)};
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
    if (auto& sink = _runtime.Policy.Broadcast)
        sink(ctx.Auth, translationKey);
}

}  // namespace VoltMod
