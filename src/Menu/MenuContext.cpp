#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Menu/MenuContext.hpp>
#include <CS2Kit/Players/Player.hpp>
#include <CS2Kit/Runtime.hpp>

namespace CS2Kit::Menu
{

bool MenuContext::Allowed(const std::string& permission) const
{
    auto* admin = CS2Kit::Detail::Rt().Players.GetPlayerBySlot(Admin);
    if (!admin)
        return false;

    auto& policy = CS2Kit::Detail::Rt().Policy;
    if (!permission.empty() && policy.HasPermission && !policy.HasPermission(admin->GetSteamID(), permission))
        return false;

    if (Target >= 0 && Target != Admin)
    {
        auto* target = CS2Kit::Detail::Rt().Players.GetPlayerBySlot(Target);
        if (!target)
            return false;
        if (policy.CanTarget && !policy.CanTarget(*admin, *target))
            return false;
    }
    return true;
}

std::string MenuContext::Tr(std::string_view key, Utils::Tokens tokens) const
{
    return CS2Kit::Detail::Rt().Translations.Get(std::string(key), Admin, tokens);
}

}  // namespace CS2Kit::Menu
