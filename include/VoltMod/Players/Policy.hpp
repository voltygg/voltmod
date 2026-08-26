#pragma once

#include <VoltMod/Players/Player.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Plugin-supplied policy the framework consults for permissions, targeting, and message routing.
 *
 * Set it once in OnLoad (`runtime.Policy = {...}`) and every policy-aware framework subsystem -
 * command dispatch, target resolution, action dispatch, context menu rows - picks it up from
 * there. An empty member means allow / no-op, so a plugin only fills in what it enforces.
 */
struct Policy
{
    /** Does @p steamId hold @p permission? Consulted before command handlers and action bodies run. */
    std::function<bool(int64_t steamId, const std::string& permission)> HasPermission;

    /** May @p caller act on @p target (immunity / same-team rules)? */
    std::function<bool(Player& caller, Player& target)> CanTarget;

    /** Deliver a command result or error line (e.g. as a colored chat reply);
     *  empty falls back to a plain runtime.Messages.Reply. */
    std::function<void(int slot, std::string_view message)> Reply;

    /** Announce a performed action; @p target is null for actions without one. */
    std::function<void(Player& caller, Player* target, const std::string& translationKey)> Broadcast;
};

}  // namespace VoltMod
