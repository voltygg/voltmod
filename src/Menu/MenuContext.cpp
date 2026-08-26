#include <VoltMod/Menu/MenuContext.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Runtime.hpp>
#include <optional>

namespace VoltMod
{

bool MenuContext::Allowed(std::string_view permission) const
{
    if (!Rt)
        return false;

    auto& players = Rt->Players;
    std::optional<PlayerRef> target;
    if (Target >= 0)
        target = players.RefFor(Target);

    return Rt->Policy.Authorize(players.RefFor(Admin), target, permission).has_value();
}

std::string MenuContext::Tr(std::string_view key, Tokens tokens) const
{
    if (!Rt)
        return std::string(key);
    return Rt->Translations.Get(std::string(key), Admin, tokens);
}

}  // namespace VoltMod
