#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/PlayerRef.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace VoltMod
{

/**
 * @brief A caller/target pair that cleared @ref Policy::Authorize.
 *
 * Only @ref Policy::Authorize produces one, so a function taking an `Authorized` is stating in
 * its signature that the check has already happened. Both players are connected for the length
 * of the call; @ref Target is null when the action has no target.
 */
struct Authorized
{
    Player& Caller;
    Player* Target = nullptr;
};

/**
 * @brief The one permission and targeting gate, plus the plugin's reply and broadcast sinks.
 *
 * Fill the four callbacks once in OnLoad (`runtime.Policy.HasPermission = ...`) and every
 * policy-aware framework subsystem - command dispatch, target resolution, action and effect
 * dispatch, context menu rows - goes through @ref Authorize to reach them. An unset
 * @ref CanTarget, @ref Reply or @ref Broadcast means "no rule / no sink"; an unset
 * @ref HasPermission denies, because there is then no trusted permission source.
 *
 * `Policy` is not assignable as a whole: it is constructed with the roster it resolves refs
 * against. Assign the members you enforce.
 */
class Policy
{
public:
    /** @p players must outlive the policy; the Runtime declares the roster above it. */
    explicit Policy(PlayerManager& players) : _players(players) {}

    Policy(const Policy&) = delete;
    Policy& operator=(const Policy&) = delete;

    /** Does @p steamId hold @p permission? Unset denies every permission-gated action. */
    std::function<bool(int64_t steamId, std::string_view permission)> HasPermission;

    /** May @p caller act on @p target (immunity, same-team rules)? Never consulted for the
     *  server console, which has no caller, nor for a caller targeting themselves. */
    std::function<bool(const Player& caller, const Player& target)> CanTarget;

    /** Deliver a command result or error line (e.g. as a colored chat reply); unset falls back
     *  to a plain `runtime.Messages.Reply`. */
    std::function<void(int slot, std::string_view message)> Reply;

    /** Announce a performed action. */
    std::function<void(const Authorized& who, std::string_view translationKey)> Broadcast;

    /**
     * @brief The single gate. Commands, actions, menu rows and effects call exactly this.
     *
     * Outcomes, in the order they are decided:
     *
     * | Condition                                    | Result                                   |
     * | -------------------------------------------- | ---------------------------------------- |
     * | @p caller is not connected                   | `ErrorCode::NotFound`, no Key            |
     * | @p target given but not connected            | `ErrorCode::NotFound`, Key `target.noMatch` |
     * | @p permission non-empty, @ref HasPermission unset | `ErrorCode::Denied`, Key `cmd.noPermission` (logged once) |
     * | @ref HasPermission says no                   | `ErrorCode::Denied`, Key `cmd.noPermission` |
     * | @ref CanTarget says no                       | `ErrorCode::Immune`, Key `target.immune` |
     * | otherwise                                    | the @ref Authorized pair                 |
     *
     * An empty @p permission skips the permission check. Targeting yourself is always allowed:
     * the rule lives here rather than in each plugin's @ref CanTarget, so @ref CanTarget only
     * ever answers "may this caller act on somebody else".
     *
     * Denial is a value. Nothing is nulled out to signal it, so a caller that ignores the
     * @ref Result cannot accidentally run the action anyway.
     */
    Result<Authorized> Authorize(PlayerRef caller, std::optional<PlayerRef> target, std::string_view permission) const;

private:
    PlayerManager& _players;
    /** Set once the missing-HasPermission denial has been logged, so it does not repeat for
     *  every command a player types. */
    mutable bool _missingPermissionWarned = false;
};

}  // namespace VoltMod
