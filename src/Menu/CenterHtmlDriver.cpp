#include "Menu/CenterHtmlDriver.hpp"

#include "Menu/CenterHtmlRender.hpp"

namespace VoltMod
{

CenterHtmlDriver::CenterHtmlDriver(MenuManager& menus, const MenuServices& services) : MenuDriver(menus, services) {}

bool CenterHtmlDriver::HandleInput(int slot)
{
    // Keys are the only input this driver has, so MenuOptions::Keyboard is not consulted: a
    // center-HTML menu nobody can navigate is a menu nobody can close.
    return HandleKeys(slot);
}

void CenterHtmlDriver::Present(int slot)
{
    auto* menu = Current(slot);
    if (!menu)
        return;

    // While a capture is pending, render a prompt overlay instead of the item list.
    if (auto prompt = _services.ChatInput.GetPrompt(slot))
    {
        _services.Messages.SendCenterHtml(slot, RenderCaptureOverlay(menu->Title, *prompt));
        return;
    }

    const CenterHtmlView view{
        .Describe = [this, slot](int index) { return Describe(slot, index); },
        .Crumbs = Crumbs(slot),
        .Slot = slot,
        .SelectedIndex = Selected(slot),
        .IsSubmenu = Depth(slot) > 1,
    };
    _services.Messages.SendCenterHtml(slot, RenderMenuHtml(menu, view, _services.Translations));
}

void CenterHtmlDriver::Dismiss(int slot)
{
    _services.Messages.ClearCenterHtml(slot);
}

}  // namespace VoltMod
