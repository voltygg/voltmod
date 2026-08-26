#pragma once

#include <VoltMod/Entities/PlayerController.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/TargetResolver.hpp>
#include <functional>
#include <optional>
#include <string>

namespace VoltMod
{
class Runtime;
}

namespace VoltMod::Players
{

/** A translation key returned by an action body, or nullopt to skip the broadcast. */
using OptKey = std::optional<std::string>;

/** Resolved caller/target pair handed to action bodies. */
struct ActionContext
{
    Player* Caller;
    Player* Target;
    Entities::PlayerController CallerCtrl;
    Entities::PlayerController TargetCtrl;
    /** The live runtime, so action and effect bodies reach services without a global.
     *  Include <VoltMod/Runtime.hpp> to use it. */
    Runtime& Rt;

    bool Valid() const { return Caller && Target && TargetCtrl.IsValid(); }
};

/**
 * @brief A single-target player action expressed as data.
 *
 * Bodies receive a valid, permission/policy-checked @ref ActionContext, mutate the target, and
 * return the broadcast key (or nullopt to stay silent). @ref ActionDispatcher owns the
 * `Resolve -> guard -> Broadcast` shape, so an action is just its permission string, its guards,
 * and its effect - no per-action wrapper function.
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
 * @brief Runs data-defined actions under the plugin's policy (`runtime.Policy`): the
 * permission check, the targetability check (immunity), and the broadcast sink all come
 * from there - no per-dispatcher wiring. Empty policy members skip the corresponding step.
 */
class ActionDispatcher
{
public:
    /** @p runtime supplies the roster, the controllers, and the policy; it must outlive the
     *  dispatcher. Cheap to construct, so a call site may build one per dispatch. */
    explicit ActionDispatcher(Runtime& runtime) : _runtime(runtime) {}

    /**
     * Resolve a caller+target slot pair, applying the permission and targetability policies.
     * Returns a context with `Valid() == false` if either player is missing, the caller lacks
     * @p permission (unless it is empty), or the targetability policy blocks the pair.
     */
    ActionContext Resolve(int callerSlot, int targetSlot, const std::string& permission) const;

    void Run(int callerSlot, int targetSlot, const Action& action) const;
    void Run(int callerSlot, int targetSlot, int param, const ParamAction& action) const;

private:
    /** Invoke the policy broadcast sink. */
    void Broadcast(const ActionContext& ctx, const std::string& translationKey) const;

    Runtime& _runtime;
};

}  // namespace VoltMod::Players
