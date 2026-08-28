#include <VoltMod/Menu/Html/HtmlMenuManager.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>

namespace VoltMod
{

MenuBuilder::MenuBuilder(HtmlMenuManager& menus, const std::string& title)
    : _menu(std::make_shared<MenuView>()), _menus(&menus), _policy(&menus.AccessPolicy())
{
    _menu->Title = title;
}

bool MenuBuilder::Allowed(std::string_view permission) const
{
    if (!_policy)
        return false;
    return _policy->Authorize(_admin, _target, permission).has_value();
}

std::string MenuBuilder::Tr(std::string_view key, Tokens tokens) const
{
    if (!_menus)
        return std::string(key);
    return _menus->Translation().Get(std::string(key), _admin.Slot, tokens);
}

}  // namespace VoltMod
