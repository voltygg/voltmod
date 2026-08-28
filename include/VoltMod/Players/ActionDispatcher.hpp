#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/Controller.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerRef.hpp>
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
 * wrapper function. A body that needs an engine service beyond the pawns/controllers here reaches
 * it through the plugin's own `App&`, or a per-descriptor factory function that captures the
 * runtime, not through this context.
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
 * @brief Runs data-defined actions through `Policy::Authorize`: the permission check, the
 * targetability check and the broadcast sink all come from there - no per-dispatcher wiring.
 */
class ActionDispatcher
{
public:
    /** @p policy, @p players and @p entities must outlive the dispatcher. Cheap to construct
     *  (three references), so a call site may build one per dispatch or hold one as a long-lived
     *  member (see @ref HtmlMenuManager, which owns one for its context rows). */
    ActionDispatcher(Policy& policy, PlayerManager& players, EntitySystem& entities)
        : _policy(policy), _players(players), _entities(entities)
    {}

    /**
     * Authorize a caller+target pair and build the context for it.
     *
     * Takes @ref PlayerRef, not slots, and hands them to `Policy::Authorize` unchanged: a stored
     * row or callback that outlived its player is refused rather than retargeted at whoever
     * occupies the slot now. Turn a slot into a ref at the boundary that first receives it
     * (`PlayerManager::RefFor`), not at dispatch time.
     *
     * @return the @ref Error from `Policy::Authorize` when either reference is stale or not
     *         connected, the caller lacks @p permission, or the targetability policy blocks it.
     */
    Result<ActionContext> Resolve(PlayerRef caller, PlayerRef target, std::string_view permission) const;

    void Run(PlayerRef caller, PlayerRef target, const Action& action) const;
    void Run(PlayerRef caller, PlayerRef target, int param, const ParamAction& action) const;

    /** Invoke the policy broadcast sink directly. Exposed so @ref EffectDispatcher (and other
     *  dispatch-adjacent code) can announce without repeating the sink lookup. */
    void Broadcast(const ActionContext& ctx, std::string_view translationKey) const;

private:
    Policy& _policy;
    PlayerManager& _players;
    EntitySystem& _entities;
};

}  // namespace VoltMod
