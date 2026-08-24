#include <CS2Kit/Commands/CommandSpec.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>

// Split from CommandSpec.cpp: everything here needs the live Runtime, and keeping it out
// leaves the spec's own logic (arg factories, derived usage, routing predicates) linkable
// on its own - which is what lets the tests reach it without HL2SDK.
namespace CS2Kit::Commands
{

int CommandContext::CallerSlot() const
{
    return Caller ? Caller->GetSlot() : -1;
}

CommandResult CommandContext::Ok(std::string_view key, Core::Tokens tokens) const
{
    return {CS2Kit::Detail::Rt().Translations.Get(std::string(key), CallerSlot(), tokens)};
}

CommandResult CommandContext::Fail(std::string_view key, Core::Tokens tokens) const
{
    return Ok(key, tokens);  // same wire shape; the two names say which outcome the handler meant
}

}  // namespace CS2Kit::Commands
