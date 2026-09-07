#pragma once

#include <VoltMod/Players/Player.hpp>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace VoltMod
{

/**
 * @brief Which binder runs for one declared argument.
 *
 * Derived from the handler's parameter list; plugins never write one. The enumerator name is also
 * the suffix of the argument's usage-placeholder key (`ArgKind::Target` -> `cmd.usage.target`), so
 * adding a kind adds one translation key and no switch.
 *
 * Declaration order is the order of @ref Args::All and of @ref BoundArg's alternatives; a
 * static_assert below holds the three together.
 */
enum class ArgKind : uint8_t
{
    Target,
    Targets,
    Duration,
    SteamId,
    PlayerOrSteamId,
    Int,
    U64,
    Word,
    Rest,
};

/**
 * @brief The argument types a command handler's parameter list is written in.
 *
 * A handler's signature *is* its argument spec: `Run([](Caller c, Args::Target t,
 * Args::Duration d) { ... })` declares two arguments, in that order, and the framework parses,
 * validates and binds them before the handler runs.
 *
 * The one nested namespace of types in the framework, because `Target`, `Int`, `Word` and `Rest`
 * are far too generic to carry at `VoltMod::` scope, and they appear nowhere but in a handler's
 * parameter list.
 *
 * Each type names its own @ref ArgKind, which is all the framework needs to know about it: there
 * is no separate trait table to keep in step.
 */
namespace Args
{

/** One online player, resolved through the selector grammar and filtered by `Policy::Authorize`. */
struct Target
{
    static constexpr ArgKind Kind = ArgKind::Target;
    Player* Value = nullptr;
};

/**
 * Every online player one token names, filtered by `Policy::Authorize`.
 *
 * The multi-target counterpart of @ref Target: `@all`, `@t`, `@ct` and the rest bind here, where
 * @ref Target rejects them because it can only yield one player. Never empty - a selector that
 * matches nobody the caller may act on fails to bind instead.
 */
struct Targets
{
    static constexpr ArgKind Kind = ArgKind::Targets;
    std::vector<Player*> Value;
};

/** The @ref ParseDuration grammar (`30s`/`5m`/`2h`/`7d`/`perm`). A bare number is minutes, and
 *  zero means permanent. */
struct Duration
{
    static constexpr ArgKind Kind = ArgKind::Duration;
    std::chrono::seconds Value{};
};

/** A numeric SteamID64. */
struct SteamId
{
    static constexpr ArgKind Kind = ArgKind::SteamId;
    int64_t Value = 0;
};

/**
 * An online player when the token resolves to one, otherwise the bare SteamID64.
 *
 * Resolution is tried first, so `@me` and a name fragment both work; a numeric token that matches
 * nobody online (or nobody the caller may act on) falls back to @ref SteamId with @ref Online left
 * null, which is how an offline player is addressed.
 */
struct PlayerOrSteamId
{
    static constexpr ArgKind Kind = ArgKind::PlayerOrSteamId;
    Player* Online = nullptr;
    int64_t SteamId = 0;
};

struct Int
{
    static constexpr ArgKind Kind = ArgKind::Int;
    int Value = 0;
};

/** A non-negative 64-bit id: a workshop id, or anything else too wide for @ref Int. */
struct U64
{
    static constexpr ArgKind Kind = ArgKind::U64;
    uint64_t Value = 0;
};

/** One verbatim token. `"two words"` in the message is one token. */
struct Word
{
    static constexpr ArgKind Kind = ArgKind::Word;
    std::string Value;
};

/** The remainder of the line, tokens rejoined with single spaces. Only the last argument may be a
 *  Rest, and it is what makes trailing free text (a reason) possible. */
struct Rest
{
    static constexpr ArgKind Kind = ArgKind::Rest;
    std::string Value;
};

/** An argument the caller may omit. Only trailing arguments may be Opt, and an Opt may not wrap
 *  another Opt. */
template <class T>
struct Opt
{
    std::optional<T> Value;
};

/** Every argument type, in @ref ArgKind order. The one list: @ref BoundArg takes its alternatives
 *  from it, and the static_assert below checks it against the enum. */
using All = std::tuple<Target, Targets, Duration, SteamId, PlayerOrSteamId, Int, U64, Word, Rest>;

}  // namespace Args

/** One entry of a command's derived argument descriptor. */
struct ArgDesc
{
    ArgKind Kind = ArgKind::Word;
    bool Optional = false;
};

/** The variant behind @ref BoundArg, over a list of argument types. */
template <class Tuple>
struct BoundArgFor;

template <class... A>
struct BoundArgFor<std::tuple<A...>>
{
    using Type = std::variant<std::monostate, A...>;
};

/** One bound argument. `std::monostate` is an optional argument the caller omitted. */
using BoundArg = typename BoundArgFor<Args::All>::Type;

/** Whether every type in @ref Args::All sits at the index its own @ref ArgKind names. */
template <std::size_t... I>
consteval bool ArgKindsMatchOrder(std::index_sequence<I...>)
{
    return ((static_cast<std::size_t>(std::tuple_element_t<I, Args::All>::Kind) == I) && ...);
}

static_assert(ArgKindsMatchOrder(std::make_index_sequence<std::tuple_size_v<Args::All>>{}),
              "Args::All must list the argument types in ArgKind declaration order.");

/** Whether @p T is an @ref Args::Opt. */
template <class T>
inline constexpr bool IsOptionalArg = false;
template <class T>
inline constexpr bool IsOptionalArg<Args::Opt<T>> = true;

/** A type that names its own @ref ArgKind: every type in @ref Args::All, and nothing else. */
template <class T>
concept NamesArgKind = requires {
    { T::Kind } -> std::convertible_to<ArgKind>;
};

/**
 * @brief What one handler parameter type means to the framework.
 *
 * The primary is declared and never defined, so a stray parameter type leaves `ArgTrait<T>`
 * incomplete: @ref CommandArg then reads as false instead of hard-erroring, and the compiler names
 * the offending signature.
 */
template <class T>
struct ArgTrait;

/** Every @ref Args type binds as itself and is required. */
template <NamesArgKind T>
struct ArgTrait<T>
{
    static constexpr ArgKind Kind = T::Kind;
    static constexpr bool Optional = false;
    using Bound = T;
};

/** `Opt<T>` binds exactly like `T` and may be missing. */
template <class T>
struct ArgTrait<Args::Opt<T>>
{
    static_assert(!IsOptionalArg<T>, "Args::Opt cannot wrap another Args::Opt; one marks the argument optional.");

    static constexpr ArgKind Kind = ArgTrait<T>::Kind;
    static constexpr bool Optional = true;
    using Bound = typename ArgTrait<T>::Bound;
};

/** A type usable as a command handler parameter. */
template <class T>
concept CommandArg = requires {
    { ArgTrait<T>::Kind } -> std::convertible_to<ArgKind>;
};

/** Whether every optional argument in @p A trails the required ones. */
template <class... A>
consteval bool OptionalsTrail()
{
    const std::array<bool, sizeof...(A)> optional{ArgTrait<A>::Optional...};
    bool seen = false;
    for (bool isOptional : optional)
    {
        if (isOptional)
            seen = true;
        else if (seen)
            return false;
    }
    return true;
}

/** Whether at most one @ref Args::Rest appears in @p A, as the final argument. A Rest eats the
 *  remainder of the line, so anything after it could never be reached. */
template <class... A>
consteval bool RestIsLast()
{
    const std::array<ArgKind, sizeof...(A)> kinds{ArgTrait<A>::Kind...};
    for (std::size_t i = 0; i + 1 < kinds.size(); ++i)
        if (kinds[i] == ArgKind::Rest)
            return false;
    return true;
}

/** A whole parameter list the builder accepts. Written as a concept so a test can assert that an
 *  invalid signature is rejected without compiling the invalid call. */
template <class... A>
concept CommandSignature = (CommandArg<A> && ...) && OptionalsTrail<A...>() && RestIsLast<A...>();

/** The descriptor the router binds against, derived from the handler's parameter list. */
template <class... A>
std::vector<ArgDesc> DescribeArgs()
{
    return {ArgDesc{.Kind = ArgTrait<A>::Kind, .Optional = ArgTrait<A>::Optional}...};
}

/** Recovers one handler parameter from its bound slot. */
template <class T>
struct ArgUnpack
{
    static T From(const BoundArg& bound) { return std::get<T>(bound); }
};

template <class T>
struct ArgUnpack<Args::Opt<T>>
{
    static Args::Opt<T> From(const BoundArg& bound)
    {
        if (std::holds_alternative<std::monostate>(bound))
            return {};
        return Args::Opt<T>{std::get<T>(bound)};
    }
};

}  // namespace VoltMod
