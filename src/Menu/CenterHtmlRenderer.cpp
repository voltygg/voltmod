#include "Menu/CenterHtmlRenderer.hpp"

namespace VoltMod
{

CenterHtmlRenderer::CenterHtmlRenderer(const MenuServices& services, MenuCore& core) : _services(services), _core(core)
{}

bool CenterHtmlRenderer::Attach(int slot)
{
    (void)slot;
    // Center HTML needs nothing on the client, so it draws for anyone.
    return true;
}

bool CenterHtmlRenderer::Present(int slot)
{
    ActiveMenus& menus = _core.Menus();
    auto* menu = menus.Current(slot);
    if (!menu)
        return true;

    // A pending capture replaces the item list with its prompt.
    if (auto prompt = _services.ChatInput.GetPrompt(slot))
    {
        _services.Messages.SendCenterHtml(slot, RenderCaptureOverlay(menu->Title, *prompt));
        return true;
    }

    const CenterHtmlView view{
        .Describe = [&menus, slot](int index) { return menus.Describe(slot, index); },
        .Breadcrumb = menus.Breadcrumb(slot),
        .Slot = slot,
        .SelectedIndex = menus.Selected(slot),
        .IsSubmenu = menus.Depth(slot) > 1,
    };
    _services.Messages.SendCenterHtml(slot, RenderMenuHtml(menu, view, _services.Translations));
    return true;
}

void CenterHtmlRenderer::Dismiss(int slot)
{
    _services.Messages.ClearCenterHtml(slot);
}

}  // namespace VoltMod
