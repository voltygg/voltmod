#include <CS2Kit/Core/CoreServices.hpp>
#include <CS2Kit/Menu/MenuContext.hpp>
#include <CS2Kit/Players/Player.hpp>
#include <CS2Kit/Players/Roster.hpp>
#include <CS2Kit/Utils/UtilsServices.hpp>

namespace CS2Kit::Menu
{

bool MenuContext::Allowed(const std::string& permission) const
{
    auto* admin = Players::Roster().GetPlayerBySlot(Admin);
    if (!admin)
        return false;

    auto& policy = Core::Ctx().Policy;
    if (!permission.empty() && policy.HasPermission && !policy.HasPermission(admin->GetSteamID(), permission))
        return false;

    if (Target >= 0 && Target != Admin)
    {
        auto* target = Players::Roster().GetPlayerBySlot(Target);
        if (!target)
            return false;
        if (policy.CanTarget && !policy.CanTarget(*admin, *target))
            return false;
    }
    return true;
}

std::string MenuContext::Tr(std::string_view key, Utils::Tokens tokens) const
{
    return Utils::Ctx().Translations.Get(std::string(key), Admin, tokens);
}

}  // namespace CS2Kit::Menu
