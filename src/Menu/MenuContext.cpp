#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Menu/MenuContext.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Runtime.hpp>

namespace VoltMod::Menu
{

bool MenuContext::Allowed(const std::string& permission) const
{
    auto* admin = VoltMod::Detail::Rt().Players.GetPlayerBySlot(Admin);
    if (!admin)
        return false;

    auto& policy = VoltMod::Detail::Rt().Policy;
    if (!permission.empty() && policy.HasPermission && !policy.HasPermission(admin->GetSteamID(), permission))
        return false;

    if (Target >= 0 && Target != Admin)
    {
        auto* target = VoltMod::Detail::Rt().Players.GetPlayerBySlot(Target);
        if (!target)
            return false;
        if (policy.CanTarget && !policy.CanTarget(*admin, *target))
            return false;
    }
    return true;
}

std::string MenuContext::Tr(std::string_view key, Core::Tokens tokens) const
{
    return VoltMod::Detail::Rt().Translations.Get(std::string(key), Admin, tokens);
}

}  // namespace VoltMod::Menu
