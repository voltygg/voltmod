#pragma once

#include <VoltMod/Commands/Args.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Core/Text/Translations.hpp>
#include <VoltMod/Players/Player.hpp>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace VoltMod
{

/**
 * @brief What a command handler answers with.
 *
 * An empty @ref Text sends no reply.
 */
struct Reply
{
    std::string Text;

    /** Handled, with nothing more to say. */
    static Reply Silent() { return {}; }
};

/**
 * @brief Who invoked the command, and how to answer them.
 *
 * A handler's first parameter. @ref Player is null and @ref Slot is -1 for console commands;
 * reply helpers use the caller's language.
 */
struct Caller
{
    /** The player who typed the command, or null for console commands. */
    VoltMod::Player* Player = nullptr;
    /** @ref Player's slot, or -1 for the console - which is also the server-language slot. */
    int Slot = -1;
    Translations& Tr;
    /** Sends a reply to chat or the console for the duration of the handler call. */
    std::function<void(const std::string&)> Send;

    /** True when the server ran the command: the server console, rcon or a cfg file. */
    bool IsServer() const { return Player == nullptr; }

    /** Return localized @p key with `{token}` substitution. */
    std::string Text(std::string_view key, Tokens tokens = {}) const;

    /** Succeed, replying with @p key localized for this caller. */
    Result<Reply> Ok(std::string_view key, Tokens tokens = {}) const;

    /**
     * Fail, replying with @p key localized for this caller.
     *
     * The error keeps @p key in `Key` and the localized line in `Text`. Errors from other
     * services carry only a key and are localized by dispatch.
     */
    std::unexpected<Error> Fail(std::string_view key, Tokens tokens = {}) const;

    /** Send one extra localized line; finish a multi-line reply with `Ok` or `Reply::Silent`. */
    void Say(std::string_view key, Tokens tokens = {}) const;

    /** Send @p line verbatim: an already-formatted row that has no translation of its own. */
    void SayRaw(std::string_view line) const;
};

/** Who may run a command, and so where it can be typed. */
enum class CommandAccess : uint8_t
{
    /** Players, from chat. The default. */
    Players,
    /** Players from chat or their own console, and the server from its console, rcon and cfg files. */
    Anywhere,
    /** The server only, from its console, rcon and cfg files. Players are ignored. */
    ServerOnly,
};

/**
 * @brief One command as the builder assembled it.
 *
 * The engine-free command definition built by @ref CommandBuilder and installed by
 * @ref CommandManager.
 */
struct CommandDefinition
{
    std::string Name;
    std::vector<std::string> Aliases;
    std::string Description;
    /** Empty means no permission check. Never checked when the server runs the command. */
    std::string PermissionName;
    /** Translation key for the whole usage line; empty derives one from @ref Args. */
    std::string UsageKey;
    CommandAccess Access = CommandAccess::Players;
    std::vector<ArgDesc> Args;
    /** The type-erased handler: unpacks @p bound back into the parameter list it was written
     *  with. Built by @ref CommandBuilder::Run. */
    std::function<Result<Reply>(const Caller&, std::span<const BoundArg>)> Invoke;
};

/** Marker holding the parameter types recovered from a handler's signature. */
template <class... A>
struct CommandArgList
{};

/** A callable with exactly one `operator()` to name: a plain lambda or functor, but not a generic
 *  one (`auto` parameters) and not one carrying overloads. */
template <class F>
concept HasOneCallOperator = requires { &std::remove_reference_t<F>::operator(); };

/**
 * @brief The parameter list of a handler, after the leading @ref Caller.
 *
 * @ref CommandBuilder::Run deduces `A...` from the callable, so the signature is the argument
 * specification. The specializations below cover the shapes a handler is written in - a lambda, a
 * functor, a function - and anything else lands on the primary and says so.
 */
template <class F>
struct CommandHandlerArgs
{
    static_assert(false,
                  "A command handler takes (Caller, Args::...) and returns Result<Reply>. A generic lambda "
                  "cannot be one: its parameter list is the argument specification, so the types have to be "
                  "written out.");
};

/** A lambda or functor, through the one `operator()` it has. */
template <HasOneCallOperator F>
struct CommandHandlerArgs<F> : CommandHandlerArgs<decltype(&std::remove_reference_t<F>::operator())>
{};

/** The one place the list is named; every shape below reduces to this. */
template <class R, class... A>
struct CommandHandlerArgs<R(Caller, A...)>
{
    using List = CommandArgList<A...>;
};

/** @{ A lambda's `operator()`, and a function pointer. `Run` decays the callable first, so a
 *  function passed by name arrives here as a pointer. */
template <class C, class R, class... A>
struct CommandHandlerArgs<R (C::*)(Caller, A...) const> : CommandHandlerArgs<R(Caller, A...)>
{};

template <class C, class R, class... A>
struct CommandHandlerArgs<R (C::*)(Caller, A...) const noexcept> : CommandHandlerArgs<R(Caller, A...)>
{};

template <class C, class R, class... A>
struct CommandHandlerArgs<R (C::*)(Caller, A...)> : CommandHandlerArgs<R(Caller, A...)>
{};

template <class C, class R, class... A>
struct CommandHandlerArgs<R (C::*)(Caller, A...) noexcept> : CommandHandlerArgs<R(Caller, A...)>
{};

template <class R, class... A>
struct CommandHandlerArgs<R (*)(Caller, A...)> : CommandHandlerArgs<R(Caller, A...)>
{};

template <class R, class... A>
struct CommandHandlerArgs<R (*)(Caller, A...) noexcept> : CommandHandlerArgs<R(Caller, A...)>
{};
/** @} */

/**
 * @brief Fluent command registration, returned by @ref CommandManager::Add.
 *
 * @code
 * commands.Add("ban")
 *     .Describe("Ban a player.")
 *     .Alias("b")
 *     .Permission("admin.ban")
 *     .Run([&app](Caller c, Args::Target t, Args::Duration d, Args::Opt<Args::Rest> why)
 *              -> Result<Reply> {
 *         std::string name = t.Value->Name();   // capture first: a ban drops the target
 *         std::string reason = why.Value ? why.Value->Value : c.Tr.Get("reason.bannedByAdmin");
 *         if (!app.Ban(*c.Player, *t.Value, reason, d.Value))
 *             return c.Fail("cmd.banFailed");
 *         return c.Ok("cmd.banSuccess", {{"name", name}});
 *     });
 * @endcode
 *
 * @ref Run installs the command. The manager owns it for the plugin load cycle.
 */
class CommandBuilder
{
public:
    /** How @ref Run hands the finished definition back to the manager that made this builder. */
    using Installer = std::function<void(CommandDefinition)>;

    CommandBuilder(Installer install, std::string_view name) : _install(std::move(install))
    {
        _def.Name = std::string(name);
    }

    /** Another name for the same command. Collisions are refused and logged at registration. */
    CommandBuilder& Alias(std::string_view alias)
    {
        _def.Aliases.emplace_back(alias);
        return *this;
    }

    /** Operator-facing description; also the console command's help text. */
    CommandBuilder& Describe(std::string_view text)
    {
        _def.Description = std::string(text);
        return *this;
    }

    /** Gate on `Policy::Authorize`. An unset `Policy::HasPermission` denies every one of these. */
    CommandBuilder& Permission(std::string_view permission)
    {
        _def.PermissionName = std::string(permission);
        return *this;
    }

    /** Also register a tier1 ConCommand, so players can type it in their console and the server
     *  can run it from its console, rcon, cfg files and `ExecuteServerCommand`. */
    CommandBuilder& Anywhere()
    {
        _def.Access = CommandAccess::Anywhere;
        return *this;
    }

    /** Register only the ConCommand, for an operator command players cannot run. */
    CommandBuilder& ServerOnly()
    {
        _def.Access = CommandAccess::ServerOnly;
        return *this;
    }

    /** Translation key for the whole usage line, replacing the one derived from the argument
     *  types. */
    CommandBuilder& UsageKey(std::string_view key)
    {
        _def.UsageKey = std::string(key);
        return *this;
    }

    /**
     * Install the command. The handler takes a @ref Caller and one `Args::` value per argument;
     * that list defines arity, parsing, and usage.
     *
     * The command is unregistered when @ref CommandManager is destroyed.
     */
    template <class F>
    void Run(F&& handler)
    {
        Bind(std::forward<F>(handler), typename CommandHandlerArgs<std::decay_t<F>>::List{});
    }

private:
    template <class F, class... A>
    void Bind(F&& handler, CommandArgList<A...>)
    {
        static_assert((CommandArg<A> && ...), "Every command handler parameter after Caller must be an Args:: type.");
        static_assert(OptionalsTrail<A...>(), "Only trailing command arguments may be Args::Opt.");
        static_assert(RestIsLast<A...>(), "Args::Rest must be the last command argument.");

        _def.Args = DescribeArgs<A...>();
        _def.Invoke = [fn = std::function<Result<Reply>(Caller, A...)>(std::forward<F>(handler))](
                          const Caller& caller, std::span<const BoundArg> bound) {
            return Unpack(fn, caller, bound, std::index_sequence_for<A...>{});
        };
        _install(std::move(_def));
    }

    /** The trampoline: one `std::get` per declared argument, in descriptor order. */
    template <class... A, std::size_t... I>
    static Result<Reply> Unpack(const std::function<Result<Reply>(Caller, A...)>& fn, const Caller& caller,
                                std::span<const BoundArg> bound, std::index_sequence<I...>)
    {
        return fn(caller, ArgUnpack<A>::From(bound[I])...);
    }

    Installer _install;
    CommandDefinition _def;
};

}  // namespace VoltMod
