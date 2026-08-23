#pragma once

#include <CS2Kit/Core/Translations.hpp>
#include <CS2Kit/Players/Player.hpp>
#include <CS2Kit/Players/Targeting.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace CS2Kit::Commands
{

struct CommandResult
{
    bool Success = true;
    std::string Message;
};

/**
 * @brief Declarative chat-command definition.
 *
 * A command is one aggregate: name, metadata, permission, typed arguments, and a handler that
 * receives everything pre-resolved. Register from a function that is handed both the command
 * table and whatever the handler needs, so the dependency is visible at the call site:
 *
 * @code
 * void RegisterBanCommands(CommandManager& commands, PunishmentService& punishments)
 * {
 * commands.Register({
 *     .Name = "ban",
 *     .Description = "Ban a player.",
 *     .Usage = "!ban <target> <duration> [reason]",
 *     .Permission = Flag(Permission::Ban),
 *     .Args = {Target(), Duration(), ReasonTail("reason.bannedByAdmin")},
 *     .Handler = [&punishments](CommandContext& c) {
 *         std::string name = c.Target->GetName();
 *         if (!punishments.IssueBan(*c.Caller, *c.Target, c.Reason, c.DurationSec))
 *             return c.Fail("cmd.banFailed");
 *         return c.Ok("cmd.banSuccess", {{"name", name}});
 *     },
 * });
 * }
 * @endcode
 *
 * Argument resolution runs before the handler: targets are resolved through the selector
 * grammar (with immunity from runtime.Policy), durations/SteamIDs are parsed and validated,
 * and failures reply with localized messages from the reserved keys `target.noMatch`,
 * `target.immune`, `target.ambiguous`, `target.dead`, `target.bot`, `cmd.badDuration`,
 * `cmd.badSteamId` (override per-arg via ArgSpec::ErrorKey).
 */
enum class ArgKind : uint8_t
{
    Target,           ///< online player(s) via the selector grammar -> Context.Target / .Targets
    TargetOrSteamId,  ///< online player -> Target + SteamId; bare numeric -> SteamId only (offline)
    Duration,         ///< ParseDuration grammar -> Context.DurationSec (bare numbers = minutes)
    SteamId64,        ///< numeric SteamID64 -> Context.SteamId
    Int,              ///< -> Context.IntValue
    Word,             ///< single verbatim token -> Context.Word
    ReasonTail,       ///< joins all remaining tokens -> Context.Reason (FallbackKey when absent)
};

struct ArgSpec
{
    ArgKind Kind = ArgKind::Word;
    bool Required = true;
    Players::TargetRules Targeting{};   ///< Kind == Target
    bool BareNumbersAreMinutes = true;  ///< Kind == Duration ("!ban x 30" = 30 minutes)
    std::string FallbackKey;            ///< Kind == ReasonTail: server-language key for the default reason
    std::string ErrorKey;               ///< overrides the default parse-failure message key
};

// Terse factories for the common arg shapes (preferred over raw ArgSpec at call sites).
ArgSpec Target(Players::TargetRules rules = {});
ArgSpec TargetOrSteamId();
ArgSpec Duration();
ArgSpec SteamId64(std::string errorKey = {});
ArgSpec Int();
ArgSpec Word(bool required = true);
ArgSpec ReasonTail(std::string fallbackKey = {});

/** Everything a handler needs, pre-resolved and pre-validated. */
struct CommandContext
{
    Players::Player* Caller = nullptr;
    Players::Player* Target = nullptr;      ///< single-target arg result
    std::vector<Players::Player*> Targets;  ///< multi-target results (TargetRules::AllowMultiple)
    int64_t SteamId = 0;
    int64_t DurationSec = 0;
    int IntValue = 0;
    std::string Word;
    std::string Reason;
    std::vector<std::string> RawArgs;

    int CallerSlot() const;

    /** Localized result helpers: translate @p key in the caller's language with @p tokens. */
    CommandResult Ok(std::string_view key, Core::Tokens tokens = {}) const;
    CommandResult Fail(std::string_view key, Core::Tokens tokens = {}) const;
};

struct CommandSpec
{
    std::string Name;
    std::vector<std::string> Aliases;
    std::string Description;
    std::string Usage;
    std::string Permission;  ///< empty = no permission required
    std::vector<ArgSpec> Args;
    std::function<CommandResult(CommandContext&)> Handler;

    bool Matches(const std::string& nameOrAlias) const;
};

}  // namespace CS2Kit::Commands
