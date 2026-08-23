#include <CS2Kit/Core/Services.hpp>
#include <CS2Kit/Menu/Menu.hpp>
#include <CS2Kit/Menu/MenuManager.hpp>
#include <CS2Kit/Menu/Options/InputOption.hpp>
#include <CS2Kit/Menu/Options/SubmenuOption.hpp>
#include <CS2Kit/Sdk/ChatInputCapture.hpp>

using CS2Kit::Core::Engine;

namespace CS2Kit::Menu
{

void SubmenuOption::OnActivate(int slot)
{
    if (!_enabled || !_factory)
        return;

    auto submenu = _factory(slot);
    if (submenu)
        Engine().Menus.OpenMenu(slot, submenu);
}

void InputOption::OnActivate(int slot)
{
    if (!_enabled)
        return;

    auto setter = _set;
    int maxLen = _maxLength;

    Engine().Sdk.ChatInput.BeginCapture(slot, _prompt, [setter, maxLen](int s, std::string_view text) -> bool {
        if (maxLen > 0 && static_cast<int>(text.size()) > maxLen)
            return false;
        if (!setter)
            return true;
        return setter(s, text);
    });
}

}  // namespace CS2Kit::Menu
