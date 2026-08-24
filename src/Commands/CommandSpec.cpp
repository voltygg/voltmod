#include <CS2Kit/Commands/CommandSpec.hpp>

namespace CS2Kit::Commands
{

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

namespace
{

/** The placeholder shown for each kind. Names, not types: `<target>` reads better than
 *  `<player>` and `<reason>` better than `<text>`. */
std::string_view Placeholder(ArgKind kind)
{
    switch (kind)
    {
    case ArgKind::Target:
        return "target";
    case ArgKind::TargetOrSteamId:
        return "target|steamId";
    case ArgKind::Duration:
        return "duration";
    case ArgKind::SteamId64:
        return "steamId";
    case ArgKind::Int:
        return "number";
    case ArgKind::ReasonTail:
        return "reason";
    case ArgKind::Word:
    default:
        return "value";
    }
}

}  // namespace

std::string DeriveUsage(const CommandSpec& spec, std::string_view prefix)
{
    std::string usage = std::string(prefix) + spec.Name;
    for (const ArgSpec& arg : spec.Args)
    {
        usage += arg.Required ? " <" : " [";
        usage += Placeholder(arg.Kind);
        usage += arg.Required ? '>' : ']';
    }
    return usage;
}

bool ReachableFrom(const CommandSpec& spec, Surface surface)
{
    return HasSurface(spec.Surfaces, surface);
}

bool TooManyArguments(const CommandSpec& spec, size_t argCount)
{
    // A ReasonTail swallows the remainder, so anything with one can never have extras.
    for (const ArgSpec& arg : spec.Args)
        if (arg.Kind == ArgKind::ReasonTail)
            return false;

    return argCount > spec.Args.size();
}

}  // namespace CS2Kit::Commands
