#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <utility>

// std::expected is the whole point of this header; a toolchain without it would silently fall
// back to nothing usable, so fail at the include instead.
static_assert(__cpp_lib_expected >= 202202L, "VoltMod requires std::expected (C++23, P0323R12).");

namespace VoltMod
{

/**
 * @brief Why an operation did not produce a value.
 *
 * Coarse on purpose: the code is what a caller branches on, @ref Error::Detail is what a log line
 * says. Adding a code is an API change for every switch over it, so prefer a better Detail.
 */
enum class ErrorCode : uint8_t
{
    NotFound,     ///< The named thing does not exist (no such player, convar, row).
    NotReady,     ///< It exists but the subsystem behind it is not usable yet.
    Invalid,      ///< The caller's input does not parse or does not satisfy a precondition.
    Denied,       ///< The caller lacks the permission the operation requires.
    Immune,       ///< The target outranks the caller; a Denied that names the target, not the verb.
    Unsupported,  ///< The build, the game version, or the gamedata does not offer this at all.
    Engine,       ///< The engine or SDK refused, returned nothing, or is not where it was expected.
    Failed        ///< Anything else; the default so a value-initialized Error is never a false success.
};

/**
 * @brief One failure: a code to branch on, text for the log, and an optional translation key.
 *
 * @ref Detail is operator-facing and never translated - it names offsets, convars, and SQL state.
 * @ref Key is the translation key a player-facing reply uses; it is empty whenever the failure has
 * nothing sensible to say to a player. A handler that replies looks at Key, logs Detail, and does
 * not invent one from the other.
 */
struct Error
{
    ErrorCode Code = ErrorCode::Failed;
    std::string Detail;
    std::string Key;

    static Error NotFound(std::string detail) { return {ErrorCode::NotFound, std::move(detail), {}}; }
    static Error NotReady(std::string detail) { return {ErrorCode::NotReady, std::move(detail), {}}; }
    static Error Invalid(std::string detail) { return {ErrorCode::Invalid, std::move(detail), {}}; }

    /** @p key is a translation key: these two are the failures a player is told about. */
    static Error Denied(std::string key) { return {ErrorCode::Denied, "permission denied", std::move(key)}; }
    static Error Immune(std::string key) { return {ErrorCode::Immune, "target is immune", std::move(key)}; }

    static Error Unsupported(std::string detail) { return {ErrorCode::Unsupported, std::move(detail), {}}; }
    static Error Engine(std::string detail) { return {ErrorCode::Engine, std::move(detail), {}}; }
    static Error Failed(std::string detail) { return {ErrorCode::Failed, std::move(detail), {}}; }
};

/** A @p T or an @ref Error. Check with `if (result)`, read `*result` / `result.error()`. */
template <class T>
using Result = std::expected<T, Error>;

/** A @ref Result with nothing to return. `return {};` succeeds. */
using Status = std::expected<void, Error>;

}  // namespace VoltMod
