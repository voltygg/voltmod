#include <VoltMod/Commands/CommandSpec.hpp>

// Kept apart from CommandSpec.cpp so the reply helpers read as one unit: a null Tr returns the
// key verbatim, which is what lets a test build a context with no framework behind it.
namespace VoltMod
{

int CommandContext::CallerSlot() const
{
    return Caller ? Caller->GetSlot() : -1;
}

CommandResult CommandContext::Ok(std::string_view key, Tokens tokens) const
{
    if (!Tr)
        return {std::string(key)};
    return {Tr->Get(std::string(key), CallerSlot(), tokens)};
}

CommandResult CommandContext::Fail(std::string_view key, Tokens tokens) const
{
    return Ok(key, tokens);  // same wire shape; the two names say which outcome the handler meant
}

}  // namespace VoltMod
