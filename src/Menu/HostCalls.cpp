#include "Menu/HostCalls.hpp"

#include <VoltMod/Menu/MenuHost.hpp>
#include <utility>

namespace VoltMod::Internal
{

void OpenMenu(MenuHost& menus, int slot, std::shared_ptr<Menu> menu)
{
    menus.OpenMenu(slot, std::move(menu));
}

void BeginInput(MenuHost& menus, int slot, std::string prompt, std::function<bool(int, std::string_view)> callback)
{
    menus.BeginInput(slot, std::move(prompt), std::move(callback));
}

}  // namespace VoltMod::Internal
