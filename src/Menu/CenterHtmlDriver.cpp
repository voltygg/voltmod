#include "Menu/CenterHtmlDriver.hpp"

#include "Menu/CenterHtmlRender.hpp"

namespace VoltMod
{

CenterHtmlDriver::CenterHtmlDriver(ActiveMenus& menus, MenuSession& session, const MenuServices& services)
    : MenuDriver(menus, session, services)
{}

void CenterHtmlDriver::Present(int slot)
{
    auto* menu = _menus.Current(slot);
    if (!menu)
        return;

    // A pending capture replaces the item list with its prompt.
    if (auto prompt = _services.ChatInput.GetPrompt(slot))
    {
        _services.Messages.SendCenterHtml(slot, RenderCaptureOverlay(menu->Title, *prompt));
        return;
    }

    const CenterHtmlView view{
        .Describe = [this, slot](int index) { return _menus.Describe(slot, index); },
        .Breadcrumb = _menus.Breadcrumb(slot),
        .Slot = slot,
        .SelectedIndex = _menus.Selected(slot),
        .IsSubmenu = _menus.Depth(slot) > 1,
    };
    _services.Messages.SendCenterHtml(slot, RenderMenuHtml(menu, view, _services.Translations));
}

void CenterHtmlDriver::Dismiss(int slot)
{
    _services.Messages.ClearCenterHtml(slot);
}

}  // namespace VoltMod
