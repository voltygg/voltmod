#include "Menu/CenterHtmlRender.hpp"
#include "Menu/MenuCore.hpp"

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <memory>
#include <utility>

namespace VoltMod
{

MenuManager::MenuManager(const MenuServices& services)
    : _services(services), _core(std::make_unique<MenuCore>(_services, *this))
{
    _core->EveryFrame([this] { OnGameFrame(); });
}

MenuManager::~MenuManager() = default;

void MenuManager::Open(int slot, std::shared_ptr<Menu> menu, MenuOptions options)
{
    // Keys are the only input center HTML has: a menu nobody can navigate is one nobody can close.
    options.Keyboard = true;
    _core->Start(slot, std::move(menu), options);
}

void MenuManager::Open(int slot, std::shared_ptr<Menu> menu)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    if (!_core->IsOpen(slot))
    {
        Open(slot, std::move(menu), {});
        return;
    }

    _core->Push(slot, std::move(menu));
}

void MenuManager::Close(int slot)
{
    if (_core->Close(slot) == MenuCore::CloseResult::Emptied)
        Dismiss(slot);
}

void MenuManager::CloseAll(int slot)
{
    if (_core->CloseAll(slot) == MenuCore::CloseResult::Emptied)
        Dismiss(slot);
}

void MenuManager::CloseAll(int slot, std::string_view replyKey)
{
    // Reply before closing: it is addressed to a player whose menus are about to go.
    _core->Reply(slot, replyKey);
    CloseAll(slot);
}

void MenuManager::Prompt(int slot, std::string prompt, std::function<bool(int, std::string_view)> callback)
{
    _core->Prompt(slot, std::move(prompt), std::move(callback));
}

std::string MenuManager::Translate(int slot, std::string_view key, std::string_view fallback) const
{
    return _core->Translate(slot, key, fallback);
}

bool MenuManager::IsOpen(int slot) const
{
    return _core->IsOpen(slot);
}

void MenuManager::FreezeWhileOpen(bool enabled)
{
    _core->FreezeWhileOpen(enabled);
}

void MenuManager::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (!_core->IsOpen(slot))
            continue;

        _core->HandleKeys(slot, ItemsPerPage, {});
        // Input may have activated a row that closed the menu it was about to draw.
        if (_core->IsOpen(slot))
            Present(slot);
    }
}

void MenuManager::Present(int slot)
{
    ActiveMenus& menus = _core->Menus();
    auto* menu = menus.Current(slot);
    if (!menu)
        return;

    // A pending capture replaces the item list with its prompt.
    if (auto prompt = _services.ChatInput.GetPrompt(slot))
    {
        _services.Messages.SendCenterHtml(slot, RenderCaptureOverlay(menu->Title, *prompt));
        return;
    }

    const CenterHtmlView view{
        .Describe = [&menus, slot](int index) { return menus.Describe(slot, index); },
        .Breadcrumb = menus.Breadcrumb(slot),
        .Slot = slot,
        .SelectedIndex = menus.Selected(slot),
        .IsSubmenu = menus.Depth(slot) > 1,
    };
    _services.Messages.SendCenterHtml(slot, RenderMenuHtml(menu, view, _services.Translations));
}

void MenuManager::Dismiss(int slot)
{
    _services.Messages.ClearCenterHtml(slot);
}

}  // namespace VoltMod
