#include <CS2Kit/Commands/CommandSpec.hpp>
#include <CS2Kit/Core/StringUtils.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>

namespace CS2Kit::Commands
{

using Core::StringUtils;

ArgSpec Target(Players::TargetRules rules)
{
    return {.Kind = ArgKind::Target, .Targeting = rules};
}

ArgSpec TargetOrSteamId()
{
    return {.Kind = ArgKind::TargetOrSteamId};
}

ArgSpec Duration()
{
    return {.Kind = ArgKind::Duration};
}

ArgSpec SteamId64(std::string errorKey)
{
    return {.Kind = ArgKind::SteamId64, .ErrorKey = std::move(errorKey)};
}

ArgSpec Int()
{
    return {.Kind = ArgKind::Int};
}

ArgSpec Word(bool required)
{
    return {.Kind = ArgKind::Word, .Required = required};
}

ArgSpec ReasonTail(std::string fallbackKey)
{
    return {.Kind = ArgKind::ReasonTail, .Required = false, .FallbackKey = std::move(fallbackKey)};
}

int CommandContext::CallerSlot() const
{
    return Caller ? Caller->GetSlot() : -1;
}

CommandResult CommandContext::Ok(std::string_view key, Core::Tokens tokens) const
{
    return {true, CS2Kit::Detail::Rt().Translations.Get(std::string(key), CallerSlot(), tokens)};
}

CommandResult CommandContext::Fail(std::string_view key, Core::Tokens tokens) const
{
    return {false, CS2Kit::Detail::Rt().Translations.Get(std::string(key), CallerSlot(), tokens)};
}

bool CommandSpec::Matches(const std::string& nameOrAlias) const
{
    const std::string lower = StringUtils::ToLower(nameOrAlias);
    if (StringUtils::ToLower(Name) == lower)
        return true;
    for (const auto& alias : Aliases)
        if (StringUtils::ToLower(alias) == lower)
            return true;
    return false;
}

}  // namespace CS2Kit::Commands
