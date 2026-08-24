#pragma once

#include <CS2Kit/Core/Translations.hpp>
#include <CS2Kit/Players/Player.hpp>
#include <CS2Kit/Players/Targeting.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CS2Kit::Commands
{

struct CommandResult
{
    std::string Message;

    /** Handled, with nothing to say: the handler already replied, or a menu is the feedback. */
    static CommandResult Silent() { return {}; }
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
 *     .Permission = Flag(Permission::Ban),
 *     .Args = {Target(), Duration(), ReasonTail("reason.bannedByAdmin")},
 *     .Handler = [&punishments](CommandContext& c) {
 *         std::string name = c.Target().GetName();
 *         if (!punishments.IssueBan(*c.Caller, c.Target(), c.Reason, c.Duration().value_or(0)))
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

/**
 * @brief Everything a handler needs, pre-resolved and pre-validated.
 *
 * The accessors are the interface; the fields behind them are what resolution filled in.
 * Resolution refuses to call the handler unless every required argument came through, so an
 * accessor for an argument the spec declared always has a value - which is why Target() hands
 * back a reference and there is nothing to null-check.
 *
 * Reading an accessor the spec did *not* declare is a bug, and reads as if the argument were
 * absent (empty, zero, nullopt) rather than crashing.
 */
struct CommandContext
{
    Players::Player* Caller = nullptr;
    Players::Player* TargetPlayer = nullptr;   ///< single-target arg result
    std::vector<Players::Player*> TargetList;  ///< multi-target results (TargetRules::AllowMultiple)
    int64_t SteamId = 0;
    std::optional<int64_t> DurationSec;
    std::optional<int> IntValue;
    std::string Word;
    std::string Reason;
    std::vector<std::string> RawArgs;

    int CallerSlot() const;

    /** The resolved target. Valid whenever the spec declared a Target()/TargetOrSteamId() that
     *  matched an online player - which is the only way the handler runs. */
    Players::Player& Target() const { return *TargetPlayer; }

    /** True when a target was resolved. Only interesting for TargetOrSteamId(), where a bare
     *  SteamID addresses someone who is not here. */
    bool HasTarget() const { return TargetPlayer != nullptr; }

    /** Every matched target, for a spec using TargetRules::AllowMultiple. */
    const std::vector<Players::Player*>& Targets() const { return TargetList; }

    /** The parsed duration in seconds, or nullopt when the caller supplied none. 0 is a real
     *  value meaning permanent, which is why this is optional rather than a sentinel. */
    std::optional<int64_t> Duration() const { return DurationSec; }

    /** The parsed integer, or nullopt when the caller supplied none. */
    std::optional<int> Int() const { return IntValue; }

    /** Localized result helpers: translate @p key in the caller's language with @p tokens. */
    CommandResult Ok(std::string_view key, Core::Tokens tokens = {}) const;
    CommandResult Fail(std::string_view key, Core::Tokens tokens = {}) const;
};

/**
 * @brief Where a command can be invoked from.
 *
 * Console commands are registered as real tier1 ConCommands, so rcon, cfg files and
 * ExecuteServerCommand all reach them. They run with no caller: there is no immunity to
 * apply and no permission to check, because the server console already is the authority.
 * Caller-relative selectors (`@me`, `@!me`) resolve against no one and simply report no
 * match.
 */
enum class Surface : uint8_t
{
    Chat = 1 << 0,
    Console = 1 << 1,
};

constexpr Surface operator|(Surface a, Surface b)
{
    return static_cast<Surface>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr bool HasSurface(Surface set, Surface one)
{
    return (static_cast<uint8_t>(set) & static_cast<uint8_t>(one)) != 0;
}

struct CommandSpec
{
    std::string Name;
    std::vector<std::string> Aliases;
    std::string Description;
    /** Leave empty to derive from Name and Args: `!ban <target> <duration> [reason]`. */
    std::string Usage;
    std::string Permission;  ///< empty = no permission required; never checked on Console
    std::vector<ArgSpec> Args;
    Surface Surfaces = Surface::Chat;
    std::function<CommandResult(CommandContext&)> Handler;

    bool Matches(const std::string& nameOrAlias) const;
};

/** `!ban <target> <duration> [reason]`, built from the arg kinds so nothing hand-written can
 *  drift from what the command actually accepts. */
std::string DeriveUsage(const CommandSpec& spec);

}  // namespace CS2Kit::Commands
