#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod::CommandSyntax
{

/**
 * @file CommandSyntax.hpp
 * @brief How a chat line is spelled: the prefixes, and splitting one into tokens.
 *
 * Pure text, no registrations and no engine, which is what keeps the whole dispatch path
 * unit-testable. Separate from @ref CommandRouter because none of it depends on which commands
 * happen to be registered.
 */

/** Split @p text on spaces. A `"quoted run"` is one token and `\"` is a literal quote. Repeated
 *  spaces produce no blank arguments; an explicit `""` is an empty argument. */
std::vector<std::string> Tokenize(std::string_view text);

/** @p message without its command prefix, or nullopt when it carries none. */
std::optional<std::string_view> StripPrefix(std::string_view message);

/** The prefix a chat usage line shows. */
std::string_view ChatPrefix();

}  // namespace VoltMod::CommandSyntax
