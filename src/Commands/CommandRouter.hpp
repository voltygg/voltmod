#pragma once

#include "Targeting.hpp"

#include <VoltMod/Commands/CommandBuilder.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VoltMod
{

/**
 * @brief The one thing command dispatch needs a running server for.
 *
 * Everything else the router does - prefixes, quoting, aliases, arity, parsing, usage lines,
 * error keys - is plain logic over strings, so the whole dispatch path is unit-tested against
 * a stub implementation of this interface and no engine at all.
 */
class ArgBinder
{
public:
    virtual ~ArgBinder() = default;

    /** Resolve one target token against the connected players; see @ref ResolveTargets. */
    virtual std::expected<std::vector<Player*>, TargetFailure> Resolve(std::string_view token, Player* caller,
                                                                       const TargetRules& rules) = 0;
};

/** @ref ArgBinder over the live roster. Defined in TargetResolver.cpp, the engine half of
 *  targeting, so the router's own translation unit stays SDK-free. */
class EngineArgBinder final : public ArgBinder
{
public:
    EngineArgBinder(PlayerManager& players, const Policy& policy, EntitySystem& entities)
        : _players(players), _policy(policy), _entities(entities)
    {}

    std::expected<std::vector<Player*>, TargetFailure> Resolve(std::string_view token, Player* caller,
                                                               const TargetRules& rules) override;

private:
    PlayerManager& _players;
    const Policy& _policy;
    EntitySystem& _entities;
};

/** Which surface an invocation arrived on. The usage line's prefix and whether a permission is
 *  checked at all both follow from it. */
enum class Origin : uint8_t
{
    Chat,
    Console,
};

/**
 * @brief A binding failure, as the key and tokens the reply is built from.
 *
 * Kept as key plus tokens rather than a finished string so the localization happens once, at
 * reply time, in the language of whoever is being answered - and so a test can assert on the
 * key without a translation table.
 */
struct ArgError
{
    std::string Key;
    Tokens Vars;
};

/**
 * @brief Prefix matching, quoting-aware tokenizing, alias lookup, arity, and argument binding.
 *
 * Owns the registered commands. Engine-free by construction: it holds a @ref Policy and a
 * @ref Translations, both of which are plain logic, and reaches the server only through the
 * @ref ArgBinder handed to @ref Dispatch.
 */
class CommandRouter
{
public:
    CommandRouter(const Policy& policy, Translations& translations) : _policy(policy), _translations(translations) {}

    /** Split @p text on spaces. A `"quoted run"` is one token and `\"` is a literal quote.
     *  Repeated spaces produce no blank arguments; an explicit `""` is an empty argument. */
    static std::vector<std::string> Tokenize(std::string_view text);

    /** @p message without its command prefix, or nullopt when it carries none. */
    static std::optional<std::string_view> StripPrefix(std::string_view message);

    /** The prefix a chat usage line shows. */
    static std::string_view ChatPrefix();

    /** Register @p def under its lowercased name plus its aliases.
     *  @return false when the name or the registration was refused (already logged). */
    bool Add(CommandDefinition def);

    /** Drop every registration. The router is emptied as a whole or not at all: a command
     *  lives exactly as long as the manager that owns it. */
    void Clear();

    /** Look @p name up as a command name, then as an alias. */
    const CommandDefinition* Find(std::string_view name) const;

    size_t Count() const { return _commands.size(); }

    /** Registered names that declare a permission. Sorted; map order is arbitrary and a load
     *  report should not be. */
    std::vector<std::string> NamesWithPermission() const;

    /** Bind @p tokens against @p def's argument descriptor. */
    std::expected<std::vector<BoundArg>, ArgError> BindArgs(const CommandDefinition& def,
                                                            std::span<const std::string> tokens, Player* caller,
                                                            ArgBinder& binder) const;

    /** The usage line for @p def, in @p slot's language. */
    std::string Usage(const CommandDefinition& def, int slot, Origin origin) const;

    /**
     * Authorize, bind and run @p def, sending every line to @p say.
     *
     * @p caller is null for @ref Origin::Console, which is the server itself: no permission is
     * checked and caller-relative selectors match nobody.
     */
    void Dispatch(const CommandDefinition& def, Player* caller, std::span<const std::string> tokens, Origin origin,
                  ArgBinder& binder, const std::function<void(const std::string&)>& say) const;

private:
    /** How many leading arguments @p def requires. */
    static size_t RequiredArgs(const CommandDefinition& def);
    /** Whether @p def's last argument swallows the remainder of the line. */
    static bool HasRest(const CommandDefinition& def);
    /** The `{prefix}`, `{command}`, `{args}` and assembled `{usage}` tokens for @p def. */
    Tokens UsageTokens(const CommandDefinition& def, int slot, Origin origin) const;
    /** One target token to the players it names, or the key explaining why it named none. */
    std::expected<std::vector<Player*>, ArgError> ResolveArg(ArgBinder& binder, const std::string& token,
                                                             Player* caller, const TargetRules& rules) const;

    const Policy& _policy;
    Translations& _translations;

    std::unordered_map<std::string, CommandDefinition> _commands;
    /** Lowercased alias -> the lowercased command name that owns it. */
    std::unordered_map<std::string, std::string> _aliases;
};

}  // namespace VoltMod
