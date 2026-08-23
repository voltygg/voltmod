#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace CS2Kit::Players
{
class Player;
}

namespace CS2Kit::Core
{

/**
 * @brief Plugin-supplied policy the kit consults for permissions, targeting, and message routing.
 *
 * Set it once in OnLoad (`Engine().Policy = {...}`) and every policy-aware kit subsystem -
 * command dispatch, target resolution, action dispatch, context menu rows - picks it up from
 * there. An empty member means allow / no-op, so a plugin only fills in what it enforces.
 */
struct PluginPolicy
{
    /** Does @p steamId hold @p permission? Consulted before command handlers and action bodies run. */
    std::function<bool(int64_t steamId, const std::string& permission)> HasPermission;

    /** May @p caller act on @p target (immunity / same-team rules)? */
    std::function<bool(Players::Player& caller, Players::Player& target)> CanTarget;

    /** Deliver a command result or error line (e.g. as a colored chat reply);
     *  empty falls back to a plain Engine().Sdk.Messages.Reply. */
    std::function<void(int slot, std::string_view message)> Reply;

    /** Announce a performed action; @p target is null for actions without one. */
    std::function<void(Players::Player& caller, Players::Player* target, const std::string& translationKey)> Broadcast;
};

}  // namespace CS2Kit::Core
