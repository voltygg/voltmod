#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Menu/Options/InputOption.hpp>
#include <VoltMod/Menu/Options/SubmenuOption.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Sdk/Messaging/ChatInputCapture.hpp>

namespace VoltMod::Menu
{

void SubmenuOption::OnActivate(int slot)
{
    if (!_enabled || !_factory)
        return;

    auto submenu = _factory(slot);
    if (submenu)
        VoltMod::Detail::Rt().Menus.OpenMenu(slot, submenu);
}

void InputOption::OnActivate(int slot)
{
    if (!_enabled)
        return;

    auto setter = _set;
    int maxLen = _maxLength;

    VoltMod::Detail::Rt().ChatInput.BeginCapture(slot, _prompt, [setter, maxLen](int s, std::string_view text) -> bool {
        if (maxLen > 0 && static_cast<int>(text.size()) > maxLen)
            return false;
        if (!setter)
            return true;
        return setter(s, text);
    });
}

}  // namespace VoltMod::Menu
