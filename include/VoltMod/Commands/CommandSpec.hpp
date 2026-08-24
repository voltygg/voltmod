#pragma once

#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/Targeting.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod::Commands
{

struct CommandResult
{
    std::string Message;

    /** Mark the command handled without sending another reply. */
    static CommandResult Silent() { return {}; }
};

/**
 * @brief Declarative chat-command definition.
 *
 * Defines a command's metadata, permission, typed arguments, and handler. Argument
 * resolution finishes before the handler runs.
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
 * Targets are resolved through the selector
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

// Prefer these factories to raw ArgSpec values at call sites.
ArgSpec Target(Players::TargetRules rules = {});
ArgSpec TargetOrSteamId();
ArgSpec Duration();
ArgSpec SteamId64(std::string errorKey = {});
ArgSpec Int();
ArgSpec Word(bool required = true);
ArgSpec ReasonTail(std::string fallbackKey = {});

/**
 * @brief Resolved command input.
 *
 * Required arguments are present whenever the handler runs, so accessors for
 * declared arguments can return values directly.
 *
 * An accessor not declared by the spec returns its empty value.
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

    /** Resolved online target. */
    Players::Player& Target() const { return *TargetPlayer; }

    /** Whether TargetOrSteamId resolved an online player. */
    bool HasTarget() const { return TargetPlayer != nullptr; }

    /** Every matched target, for a spec using TargetRules::AllowMultiple. */
    const std::vector<Players::Player*>& Targets() const { return TargetList; }

    /** Duration in seconds. `0` means permanent; nullopt means omitted. */
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
 * Console commands are tier1 ConCommands reachable through RCON, cfg files, and
 * ExecuteServerCommand. They have no caller, permission check, or immunity check.
 * Caller-relative selectors therefore report no match.
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
};

/** Derive usage from the argument kinds. @p prefix is empty for console commands. */
std::string DeriveUsage(const CommandSpec& spec, std::string_view prefix);

/** Whether @p spec is reachable from @p surface. */
bool ReachableFrom(const CommandSpec& spec, Surface surface);

/** Whether the input has tokens the spec cannot consume. ReasonTail consumes the remainder. */
bool TooManyArguments(const CommandSpec& spec, size_t argCount);

}  // namespace VoltMod::Commands
