#include <CS2Kit/App/Services.hpp>
#include <CS2Kit/Menu/MenuContext.hpp>
#include <CS2Kit/Players/Player.hpp>
#include <CS2Kit/Players/PlayerManager.hpp>

namespace CS2Kit::Menu
{

bool MenuContext::Allowed(const std::string& permission) const
{
    auto& engine = App::Engine();
    auto* admin = engine.Players.GetPlayerBySlot(Admin);
    if (!admin)
        return false;

    auto& policy = engine.Policy;
    if (!permission.empty() && policy.HasPermission && !policy.HasPermission(admin->GetSteamID(), permission))
        return false;

    if (Target >= 0 && Target != Admin)
    {
        auto* target = engine.Players.GetPlayerBySlot(Target);
        if (!target)
            return false;
        if (policy.CanTarget && !policy.CanTarget(*admin, *target))
            return false;
    }
    return true;
}

std::string MenuContext::Tr(std::string_view key, Utils::Tokens tokens) const
{
    return App::Engine().Translations.Get(std::string(key), Admin, tokens);
}

}  // namespace CS2Kit::Menu
