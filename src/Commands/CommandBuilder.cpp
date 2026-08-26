#include <VoltMod/Commands/CommandBuilder.hpp>

// The reply helpers, kept apart from the router so they read as one unit: every one of them
// localizes for this caller's slot, which is what keeps English literals out of handlers.
namespace VoltMod
{

std::string Caller::Text(std::string_view key, Tokens tokens) const
{
    return tokens.empty() ? Tr.Get(std::string(key), Slot) : Tr.Get(std::string(key), Slot, tokens);
}

Result<Reply> Caller::Ok(std::string_view key, Tokens tokens) const
{
    return Reply{Text(key, std::move(tokens))};
}

std::unexpected<Error> Caller::Fail(std::string_view key, Tokens tokens) const
{
    // Detail carries the finished line because Error has nowhere to hold `tokens` until reply
    // time; Key still names the key, so a caller inspecting the result can branch on it.
    return std::unexpected(Error{ErrorCode::Failed, Text(key, std::move(tokens)), std::string(key)});
}

void Caller::Say(std::string_view key, Tokens tokens) const
{
    SayRaw(Text(key, std::move(tokens)));
}

void Caller::SayRaw(std::string_view line) const
{
    if (Send && !line.empty())
        Send(std::string(line));
}

}  // namespace VoltMod
