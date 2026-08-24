#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>

namespace VoltMod::Players
{

using Sdk::PlayerController;

ActionContext ActionDispatcher::Resolve(int callerSlot, int targetSlot, const std::string& permission) const
{
    ActionContext ctx{nullptr, nullptr, PlayerController(callerSlot), PlayerController(targetSlot),
                      VoltMod::Detail::Rt()};

    auto& plrMgr = VoltMod::Detail::Rt().Players;
    ctx.Caller = plrMgr.GetPlayerBySlot(callerSlot);
    ctx.Target = plrMgr.GetPlayerBySlot(targetSlot);

    if (!ctx.Caller || !ctx.Target)
        return ctx;

    auto& policy = VoltMod::Detail::Rt().Policy;
    if (!permission.empty() && policy.HasPermission && !policy.HasPermission(ctx.Caller->GetSteamID(), permission))
    {
        ctx.Caller = nullptr;
        return ctx;
    }
    if (policy.CanTarget && !policy.CanTarget(*ctx.Caller, *ctx.Target))
    {
        ctx.Caller = nullptr;
        return ctx;
    }
    return ctx;
}

void ActionDispatcher::Run(int callerSlot, int targetSlot, const Action& action) const
{
    auto ctx = Resolve(callerSlot, targetSlot, action.Permission);
    if (!ctx.Valid())
        return;
    if (action.RequireAlive && !ctx.TargetCtrl.IsAlive())
        return;
    if (auto key = action.Body(ctx))
        Broadcast(ctx, *key);
}

void ActionDispatcher::Run(int callerSlot, int targetSlot, int param, const ParamAction& action) const
{
    auto ctx = Resolve(callerSlot, targetSlot, action.Permission);
    if (!ctx.Valid())
        return;
    if (action.RequireAlive && !ctx.TargetCtrl.IsAlive())
        return;
    if (auto key = action.Body(ctx, param))
        Broadcast(ctx, *key);
}

void ActionDispatcher::Broadcast(const ActionContext& ctx, const std::string& translationKey) const
{
    auto& policy = VoltMod::Detail::Rt().Policy;
    if (!policy.Broadcast || !ctx.Caller)
        return;
    policy.Broadcast(*ctx.Caller, ctx.Target, translationKey);
}

}  // namespace VoltMod::Players
