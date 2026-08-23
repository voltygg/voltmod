#include <CS2Kit/Core/ActiveService.hpp>
#include <CS2Kit/Menu/MenuAccess.hpp>

namespace CS2Kit::Menu
{

void SetActiveMenus(MenuManager* menus)
{
    Core::ActiveService<MenuManager>::Set(menus);
}

MenuManager& Menus()
{
    return Core::ActiveService<MenuManager>::Get();
}

MenuManager* MenusOrNull()
{
    return Core::ActiveService<MenuManager>::GetOrNull();
}

}  // namespace CS2Kit::Menu
