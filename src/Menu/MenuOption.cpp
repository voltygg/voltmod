#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Menu/Options/InputOption.hpp>
#include <VoltMod/Menu/Options/SubmenuOption.hpp>

namespace VoltMod
{

void SubmenuOption::OnActivate(int slot, MenuManager& menus)
{
    if (!_enabled || !_factory)
        return;

    auto submenu = _factory(slot);
    if (submenu)
        menus.OpenMenu(slot, submenu);
}

void InputOption::OnActivate(int slot, MenuManager& menus)
{
    if (!_enabled)
        return;

    auto setter = _set;
    int maxLen = _maxLength;

    menus.BeginInput(slot, _prompt, [setter, maxLen](int s, std::string_view text) -> bool {
        if (maxLen > 0 && static_cast<int>(text.size()) > maxLen)
            return false;
        if (!setter)
            return true;
        return setter(s, text);
    });
}

}  // namespace VoltMod
