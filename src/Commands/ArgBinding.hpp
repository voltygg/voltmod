#pragma once

#include "Targeting.hpp"

#include <VoltMod/Commands/CommandBuilder.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief The one thing command dispatch needs a running server for.
 *
 * Everything else - prefixes, quoting, aliases, arity, parsing, usage lines, error keys - is plain
 * logic over strings, so the whole dispatch path is unit-tested against a stub implementation of
 * this interface and no engine at all.
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
 *  targeting, so the binding translation unit stays SDK-free. */
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

/**
 * @brief A binding failure, as the key and tokens the reply is built from.
 *
 * Kept as key plus tokens rather than a finished string so the localization happens once, at reply
 * time, in the language of whoever is being answered - and so a test can assert on the key without
 * a translation table.
 */
struct ArgError
{
    std::string Key;
    Tokens Vars;
};

/**
 * Bind @p tokens against @p def's argument descriptor.
 *
 * Arity is the caller's job: this assumes @p tokens already satisfies @p def's required count, so
 * a short list can only mean a trailing optional argument. Takes nothing from the router, which is
 * why it is a free function - the descriptor and the binder are the whole input.
 */
std::expected<std::vector<BoundArg>, ArgError> BindArgs(const CommandDefinition& def,
                                                        std::span<const std::string> tokens, Player* caller,
                                                        ArgBinder& binder);

}  // namespace VoltMod
