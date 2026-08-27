#pragma once

#include "ArgBinding.hpp"

#include <VoltMod/Commands/CommandBuilder.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VoltMod
{

/** Which surface an invocation arrived on. The usage line's prefix and whether a permission is
 *  checked at all both follow from it. */
enum class Origin : uint8_t
{
    Chat,
    Console,
};

/**
 * @brief The registered commands, their usage lines, and the dispatch that runs one.
 *
 * Engine-free by construction: it holds a @ref Policy and a @ref Translations, both of which are
 * plain logic, and reaches the server only through the @ref ArgBinder handed to @ref Dispatch.
 * Chat syntax lives in @ref CommandSyntax and argument binding in @ref BindArgs; neither needs the
 * registry, so neither is here.
 */
class CommandRouter
{
public:
    CommandRouter(const Policy& policy, Translations& translations) : _policy(policy), _translations(translations) {}

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

    const Policy& _policy;
    Translations& _translations;

    std::unordered_map<std::string, CommandDefinition> _commands;
    /** Lowercased alias -> the lowercased command name that owns it. */
    std::unordered_map<std::string, std::string> _aliases;
};

}  // namespace VoltMod
