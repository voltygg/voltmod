#pragma once

#include <VoltMod/Commands/Args.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Players/Player.hpp>
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
 * An empty @ref Text sends nothing, which is what a command whose feedback is a menu, a
 * broadcast, or its own @ref Caller::Say lines wants.
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
 * A handler's first parameter. @ref P is null on the console surface, where @ref Slot is -1
 * and every lookup resolves the server language.
 *
 * The reply helpers localize in the caller's own language, so a handler never touches
 * @ref Translations for its own replies and never formats an English literal.
 */
struct Caller
{
    /** The player who typed the command; null on the console surface. */
    Player* P = nullptr;
    /** @ref P's slot, or -1 for the console - which is also the server-language slot. */
    int Slot = -1;
    /** The table @ref Ok, @ref Fail and @ref Say translate through. Also the way to read a
     *  server-language string (`Tr.Get(key)`), which is what a reason written to the database
     *  or announced to everyone wants. */
    Translations& Tr;
    /** Where @ref Say lines go: `Policy.Reply` in chat, the console log otherwise. Owned by
     *  the dispatch frame, so it is live for exactly the length of the handler call. */
    std::function<void(const std::string&)> Send;

    /** @p key localized for this caller, with `{token}` substitution. */
    std::string Text(std::string_view key, Tokens tokens = {}) const;

    /** Succeed, replying with @p key localized for this caller. */
    Result<Reply> Ok(std::string_view key, Tokens tokens = {}) const;

    /**
     * Fail, replying with @p key localized for this caller.
     *
     * The failure is an @ref Error, so it cannot be mistaken for a success. `Error::Key` names
     * the key; the already-localized line rides in `Error::Detail`, because @ref Error has
     * nowhere to carry @p tokens to reply time. An `Error` from anywhere else - a
     * `Policy::Authorize` denial, say - carries only a `Key`, and dispatch localizes that.
     */
    std::unexpected<Error> Fail(std::string_view key, Tokens tokens = {}) const;

    /** Send one extra line now, localized for this caller. Multi-line output is a run of these
     *  followed by an `Ok` or a `Reply::Silent`. */
    void Say(std::string_view key, Tokens tokens = {}) const;

    /** Send @p line verbatim: an already-formatted row that has no translation of its own. */
    void SayRaw(std::string_view line) const;
};

/**
 * @brief One command as the builder assembled it.
 *
 * The seam between the public builder and the engine-free router. Plugins do not build one;
 * @ref CommandBuilder does, and hands it to @ref CommandManager to install.
 */
struct CommandDefinition
{
    std::string Name;
    std::vector<std::string> Aliases;
    std::string Description;
    /** Empty means no permission check. Never checked on the console surface. */
    std::string PermissionName;
    /** Translation key for the whole usage line; empty derives one from @ref Args. */
    std::string UsageKey;
    bool Chat = true;
    bool Console = false;
    std::vector<ArgDesc> Args;
    /** The type-erased handler: unpacks @p bound back into the parameter list it was written
     *  with. Built by @ref CommandBuilder::Run. */
    std::function<Result<Reply>(const Caller&, std::span<const BoundArg>)> Invoke;
};

/** Marker holding the parameter types recovered from a handler's signature. */
template <class... A>
struct CommandArgList
{};

/**
 * @brief The parameter list of a handler, after the leading @ref Caller.
 *
 * This is what makes the signature the argument spec: `Run` deduces `A...` from the callable
 * itself rather than from a separately written descriptor that could drift from it.
 */
template <class F>
struct CommandHandlerArgs : CommandHandlerArgs<decltype(&std::remove_reference_t<F>::operator())>
{};

template <class C, class R, class... A>
struct CommandHandlerArgs<R (C::*)(Caller, A...) const>
{
    using List = CommandArgList<A...>;
};

template <class C, class R, class... A>
struct CommandHandlerArgs<R (C::*)(Caller, A...)>
{
    using List = CommandArgList<A...>;
};

template <class R, class... A>
struct CommandHandlerArgs<R (*)(Caller, A...)>
{
    using List = CommandArgList<A...>;
};

/**
 * @brief Fluent command registration, returned by @ref CommandManager::Add.
 *
 * @code
 * _subs.push_back(commands.Add("ban")
 *     .Describe("Ban a player.")
 *     .Alias("b")
 *     .Permission(Flag(Permission::Ban))
 *     .Run([&app](Caller c, Args::Target t, Args::Duration d, Args::Opt<Args::Rest> why)
 *              -> Result<Reply> {
 *         std::string name = t.Value->Name();   // capture first: a ban drops the target
 *         std::string reason = why.Value ? why.Value->Value : c.Tr.Get("reason.bannedByAdmin");
 *         if (!app.Ban(*c.P, *t.Value, reason, d.Value))
 *             return c.Fail("cmd.banFailed");
 *         return c.Ok("cmd.banSuccess", {{"name", name}});
 *     }));
 * @endcode
 *
 * Single use: @ref Run consumes the builder and returns the @ref Subscription that owns the
 * registration. Hold it beside the state the handler captured.
 */
class CommandBuilder
{
public:
    /** How @ref Run hands the finished definition back to the manager that made this builder. */
    using Installer = std::function<Subscription(CommandDefinition)>;

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

    /** Also register a tier1 ConCommand of the same name, for rcon, cfg files and
     *  `ExecuteServerCommand`. The command stays typeable in chat. */
    CommandBuilder& Console()
    {
        _def.Console = true;
        return *this;
    }

    /** Register only the ConCommand. Operator commands that carry no permission belong here:
     *  the console is the server itself, chat is not. */
    CommandBuilder& ConsoleOnly()
    {
        _def.Console = true;
        _def.Chat = false;
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
     * Install the command. @p handler takes a @ref Caller and then one `Args::` value per
     * argument; that list is the argument spec - arity, order, parsing and the usage line all
     * come from it.
     *
     * @return the registration. Dropping it unregisters the command and removes its ConCommand.
     */
    template <class F>
    [[nodiscard]] Subscription Run(F&& handler)
    {
        return Bind(std::forward<F>(handler), typename CommandHandlerArgs<std::remove_cvref_t<F>>::List{});
    }

private:
    template <class F, class... A>
    Subscription Bind(F&& handler, CommandArgList<A...>)
    {
        static_assert((CommandArg<A> && ...), "Every command handler parameter after Caller must be an Args:: type.");
        static_assert(OptionalsTrail<A...>(), "Only trailing command arguments may be Args::Opt.");
        static_assert(RestIsLast<A...>(), "Args::Rest must be the last command argument.");

        _def.Args = DescribeArgs<A...>();
        _def.Invoke = [fn = std::function<Result<Reply>(Caller, A...)>(std::forward<F>(handler))](
                          const Caller& caller, std::span<const BoundArg> bound) {
            return Unpack(fn, caller, bound, std::index_sequence_for<A...>{});
        };
        return _install(std::move(_def));
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
