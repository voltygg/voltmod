#include <VoltMod/Menu/MenuContext.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Runtime.hpp>

namespace VoltMod
{

bool MenuContext::Allowed(const std::string& permission) const
{
    if (!Rt)
        return false;

    auto* admin = Rt->Players.GetPlayerBySlot(Admin);
    if (!admin)
        return false;

    auto& policy = Rt->Policy;
    if (!permission.empty() && policy.HasPermission && !policy.HasPermission(admin->GetSteamID(), permission))
        return false;

    if (Target >= 0 && Target != Admin)
    {
        auto* target = Rt->Players.GetPlayerBySlot(Target);
        if (!target)
            return false;
        if (policy.CanTarget && !policy.CanTarget(*admin, *target))
            return false;
    }
    return true;
}

std::string MenuContext::Tr(std::string_view key, Tokens tokens) const
{
    if (!Rt)
        return std::string(key);
    return Rt->Translations.Get(std::string(key), Admin, tokens);
}

}  // namespace VoltMod
