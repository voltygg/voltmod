#include <VoltMod/Commands/CommandSpec.hpp>
#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Runtime.hpp>

// Split from CommandSpec.cpp: everything here needs the live Runtime, and keeping it out
// leaves the spec's own logic (arg factories, derived usage, routing predicates) linkable
// on its own - which is what lets the tests reach it without HL2SDK.
namespace VoltMod::Commands
{

int CommandContext::CallerSlot() const
{
    return Caller ? Caller->GetSlot() : -1;
}

CommandResult CommandContext::Ok(std::string_view key, Core::Tokens tokens) const
{
    return {VoltMod::Detail::Rt().Translations.Get(std::string(key), CallerSlot(), tokens)};
}

CommandResult CommandContext::Fail(std::string_view key, Core::Tokens tokens) const
{
    return Ok(key, tokens);  // same wire shape; the two names say which outcome the handler meant
}

}  // namespace VoltMod::Commands
