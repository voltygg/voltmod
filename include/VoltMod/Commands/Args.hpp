#pragma once

#include <VoltMod/Players/Player.hpp>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace VoltMod
{

/**
 * @brief The argument types a command handler's parameter list is written in.
 *
 * A handler's signature *is* its argument spec: `Run([](Caller c, Args::Target t,
 * Args::Duration d) { ... })` declares two arguments, in that order, and the framework
 * parses, validates and binds them before the handler runs.
 *
 * The one nested namespace of types in the framework, because `Target`, `Int`, `Word` and
 * `Rest` are far too generic to carry at `VoltMod::` scope, and they appear nowhere but in a
 * handler's parameter list.
 */
namespace Args
{

/** One online player, resolved through the selector grammar and filtered by `Policy::Authorize`. */
struct Target
{
    Player* Value = nullptr;
};

/** The @ref ParseDuration grammar (`30s`/`5m`/`2h`/`7d`/`perm`). A bare number is minutes, and
 *  zero means permanent. */
struct Duration
{
    std::chrono::seconds Value{};
};

/** A numeric SteamID64. */
struct SteamId
{
    int64_t Value = 0;
};

/**
 * An online player when the token resolves to one, otherwise the bare SteamID64.
 *
 * Resolution is tried first, so `@me` and a name fragment both work; a numeric token that
 * matches nobody online (or nobody the caller may act on) falls back to @ref SteamId with
 * @ref Online left null, which is how an offline player is addressed.
 */
struct PlayerOrSteamId
{
    Player* Online = nullptr;
    int64_t SteamId = 0;
};

struct Int
{
    int Value = 0;
};

/** One verbatim token. `"two words"` in the message is one token. */
struct Word
{
    std::string Value;
};

/** The remainder of the line, tokens rejoined with single spaces. Only the last argument may
 *  be a Rest, and it is what makes trailing free text (a reason) possible. */
struct Rest
{
    std::string Value;
};

/** An argument the caller may omit. Only trailing arguments may be Opt. */
template <class T>
struct Opt
{
    std::optional<T> Value;
};

}  // namespace Args

/**
 * @brief Which binder runs for one declared argument.
 *
 * Derived from the handler's parameter list; plugins never write one. The enumerator name is
 * also the suffix of the argument's usage-placeholder key (`ArgKind::Target` ->
 * `cmd.usage.target`), so adding a kind adds one translation key and no switch.
 */
enum class ArgKind : uint8_t
{
    Target,
    Duration,
    SteamId,
    PlayerOrSteamId,
    Int,
    Word,
    Rest,
};

/** One entry of a command's derived argument descriptor. */
struct ArgDesc
{
    ArgKind Kind = ArgKind::Word;
    bool Optional = false;
};

/** One bound argument. `std::monostate` is an optional argument the caller omitted. */
using BoundArg = std::variant<std::monostate, Args::Target, Args::Duration, Args::SteamId,
                              Args::PlayerOrSteamId, Args::Int, Args::Word, Args::Rest>;

/**
 * @brief What one handler parameter type means to the framework.
 *
 * Only the types above specialize it, so a stray parameter type is a compile error naming the
 * offending signature rather than a runtime surprise.
 */
template <class T>
struct ArgTrait;

template <>
struct ArgTrait<Args::Target>
{
    static constexpr ArgKind Kind = ArgKind::Target;
    static constexpr bool Optional = false;
    using Bound = Args::Target;
};

template <>
struct ArgTrait<Args::Duration>
{
    static constexpr ArgKind Kind = ArgKind::Duration;
    static constexpr bool Optional = false;
    using Bound = Args::Duration;
};

template <>
struct ArgTrait<Args::SteamId>
{
    static constexpr ArgKind Kind = ArgKind::SteamId;
    static constexpr bool Optional = false;
    using Bound = Args::SteamId;
};

template <>
struct ArgTrait<Args::PlayerOrSteamId>
{
    static constexpr ArgKind Kind = ArgKind::PlayerOrSteamId;
    static constexpr bool Optional = false;
    using Bound = Args::PlayerOrSteamId;
};

template <>
struct ArgTrait<Args::Int>
{
    static constexpr ArgKind Kind = ArgKind::Int;
    static constexpr bool Optional = false;
    using Bound = Args::Int;
};

template <>
struct ArgTrait<Args::Word>
{
    static constexpr ArgKind Kind = ArgKind::Word;
    static constexpr bool Optional = false;
    using Bound = Args::Word;
};

template <>
struct ArgTrait<Args::Rest>
{
    static constexpr ArgKind Kind = ArgKind::Rest;
    static constexpr bool Optional = false;
    using Bound = Args::Rest;
};

/** `Opt<T>` binds exactly like `T` and may be missing. Nesting an Opt in an Opt does not
 *  compile, because `Opt<Opt<T>>` has no `ArgTrait<Opt<T>>::Bound` to name. */
template <class T>
struct ArgTrait<Args::Opt<T>>
{
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

/** A whole parameter list the builder accepts. Written as a concept so a test can assert that
 *  an invalid signature is rejected without compiling the invalid call. */
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
