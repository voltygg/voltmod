#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/Controller.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace VoltMod
{

/** A translation key returned by an action body, or nullopt to skip the broadcast. */
using OptKey = std::optional<std::string>;

/** Resolved caller/target pair handed to action bodies. */
struct ActionContext
{
    /** The pair, as @ref Policy::Authorize cleared it. @ref Caller and @ref Target read it. */
    Authorized Auth;
    /** The live runtime, so action and effect bodies reach services without a global.
     *  Include <VoltMod/Runtime.hpp> to use it. */
    Runtime& Rt;
    /** Frame-local wrappers, resolved by the dispatcher for this one dispatch. */
    Controller CallerCtrl;
    Controller TargetCtrl;

    [[nodiscard]] Player& Caller() const { return Auth.Caller; }
    /** An action always has a target: @ref ActionDispatcher::Resolve fails without one. */
    [[nodiscard]] Player& Target() const { return *Auth.Target; }

    /** @{ The pawns behind the two controllers. Free to call - each controller resolved its pawn
     *  when the dispatcher built it - and falsy when the player has none. */
    [[nodiscard]] Pawn CallerPawn() const { return CallerCtrl.GetPawn(); }
    [[nodiscard]] Pawn TargetPawn() const { return TargetCtrl.GetPawn(); }
    /** @} */
};

/**
 * @brief A single-target player action expressed as data.
 *
 * Bodies receive an authorized @ref ActionContext, mutate the target, and return the broadcast
 * key (or nullopt to stay silent). @ref ActionDispatcher owns the `Resolve -> guard -> Broadcast`
 * shape, so an action is just its permission string, its guards, and its effect - no per-action
 * wrapper function.
 */
struct Action
{
    std::string Permission;    /**< Consumer-defined permission token; "" skips the check. */
    bool RequireAlive = false; /**< Skip silently if the target is dead. */
    std::function<OptKey(const ActionContext&)> Body;
};

/** Like @ref Action but carries an integer the menu/command supplies (health, team, ...). */
struct ParamAction
{
    std::string Permission;
    bool RequireAlive = false;
    std::function<OptKey(const ActionContext&, int param)> Body;
};

/**
 * @brief Runs data-defined actions through `runtime.Policy.Authorize`: the permission check, the
 * targetability check and the broadcast sink all come from there - no per-dispatcher wiring.
 */
class ActionDispatcher
{
public:
    /** @p runtime supplies the roster, the controllers, and the policy; it must outlive the
     *  dispatcher. Cheap to construct, so a call site may build one per dispatch. */
    explicit ActionDispatcher(Runtime& runtime) : _runtime(runtime) {}

    /**
     * Authorize a caller+target slot pair and build the context for it.
     * @return the @ref Error from `Policy::Authorize` when either player is not connected, the
     *         caller lacks @p permission, or the targetability policy blocks the pair.
     */
    Result<ActionContext> Resolve(int callerSlot, int targetSlot, std::string_view permission) const;

    void Run(int callerSlot, int targetSlot, const Action& action) const;
    void Run(int callerSlot, int targetSlot, int param, const ParamAction& action) const;

private:
    /** Invoke the policy broadcast sink. */
    void Broadcast(const ActionContext& ctx, std::string_view translationKey) const;

    Runtime& _runtime;
};

}  // namespace VoltMod
