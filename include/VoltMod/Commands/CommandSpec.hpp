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
 * @brief Command metadata, typed arguments, permission, surfaces, and handler.
 *
 * The manager resolves every required argument and applies target immunity
 * before calling the handler. Parse and target failures produce localized
 * replies; ArgSpec::ErrorKey can override the default key.
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
    std::string FallbackKey;  ///< Kind == ReasonTail: server-language key for the default reason
    std::string ErrorKey;     ///< overrides the default parse-failure message key
};

// Factories for declarations at call sites.
ArgSpec Target();
ArgSpec TargetOrSteamId();
ArgSpec Duration();
ArgSpec SteamId64(std::string errorKey = {});
ArgSpec Int();
ArgSpec Word(bool required = true);
ArgSpec ReasonTail(std::string fallbackKey = {});

/**
 * @brief Resolved command input.
 *
 * Required arguments are present when the handler runs. Accessors for arguments
 * not declared by the spec return their empty value.
 *
 * Default-constructible and free of engine types, so tests can build one directly.
 */
struct CommandContext
{
    /** Localization source for @ref Ok / @ref Fail, set by the dispatching CommandManager.
     *  Null (a hand-built context) makes those helpers return the key unchanged. */
    Core::Translations* Tr = nullptr;
    Players::Player* Caller = nullptr;
    Players::Player* TargetPlayer = nullptr;   ///< single-target arg result
    std::vector<Players::Player*> TargetList;  ///< multi-target results (TargetRules::AllowMultiple)
    int64_t SteamId = 0;
    std::optional<int64_t> DurationSec;
    std::optional<int> IntValue;
    std::string Word;
    std::string Reason;

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

    /** Localized result helpers: translate @p key in the caller's language with @p tokens.
     *  Return @p key verbatim when @ref Tr is null. */
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
