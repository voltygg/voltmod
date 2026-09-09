#include "Menu/CenterHtmlRenderer.hpp"
#include "Menu/MenuCore.hpp"
#include "Menu/PanoramaRenderer.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <memory>
#include <utility>

namespace VoltMod
{

MenuManager::MenuManager(const MenuServices& services)
    : _services(services), _core(std::make_unique<MenuCore>(_services, *this))
{
    _onPanorama.BindReset(services.Slots);
    _centerHtml = std::make_unique<CenterHtmlRenderer>(_services, *_core);
    _core->EveryFrame([this] { OnGameFrame(); });
}

MenuManager::~MenuManager() = default;

void MenuManager::UsePanorama(PanoramaMenuOptions options)
{
    _panorama = std::make_unique<PanoramaRenderer>(_services, *_core, *this, options);
}

void MenuManager::Open(int slot, std::shared_ptr<Menu> menu, MenuOptions options)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    // The player's own surface is released before the new one is claimed, so closing the old
    // session cannot drop a panel this one just spawned.
    CloseAll(slot);

    const bool panorama = CanUsePanorama(slot) && _panorama->Attach(slot);
    // Keys are the only input center HTML has: a menu nobody can navigate is one nobody can close.
    if (!panorama)
        options.Keyboard = true;

    _core->Start(slot, std::move(menu), options);
    _onPanorama[slot] = panorama;
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
    // A menu pushed onto the session opens at its first page.
    RendererFor(slot).ShowPage(slot, 0);
}

void MenuManager::Close(int slot)
{
    if (!IsValidSlot(slot))
        return;

    switch (_core->Close(slot))
    {
    case MenuCore::CloseResult::Emptied:
        RendererFor(slot).Dismiss(slot);
        break;
    case MenuCore::CloseResult::Stayed:
    {
        // The parent menu is showing again: draw the page its cursor was left on.
        MenuRenderer& renderer = RendererFor(slot);
        renderer.ShowPage(slot, _core->Menus().Selected(slot) / renderer.RowsPerPage());
        break;
    }
    case MenuCore::CloseResult::NotOpen:
        break;
    }
}

void MenuManager::CloseAll(int slot)
{
    if (!IsValidSlot(slot))
        return;

    if (_core->CloseAll(slot) == MenuCore::CloseResult::Emptied)
        RendererFor(slot).Dismiss(slot);
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

bool MenuManager::IsPanorama(int slot) const
{
    return IsValidSlot(slot) && _core->IsOpen(slot) && _onPanorama[slot];
}

void MenuManager::FreezeWhileOpen(bool enabled)
{
    _core->FreezeWhileOpen(enabled);
}

MenuRenderer& MenuManager::RendererFor(int slot)
{
    return _onPanorama[slot] ? *_panorama : *_centerHtml;
}

bool MenuManager::CanUsePanorama(int slot) const
{
    if (!_panorama)
        return false;

    for (Capability capability : {Capability::CustomUi, Capability::UiClicks, Capability::Visibility})
    {
        if (!_services.Capabilities.Has(capability))
            return false;
    }

    // A client still fetching the menu addon has no layout to draw the panel on yet.
    return _services.Addons.Pending(slot).empty();
}

void MenuManager::MoveToCenterHtml(int slot)
{
    Log::Info("Menu: slot {} lost its Panorama panel; drawing center HTML for the rest of the session.", slot);
    _panorama->Dismiss(slot);
    _onPanorama[slot] = false;
    // The session may have been opened click-only; center HTML has nothing but the keys.
    _core->Menus().State(slot).Keyboard = true;
    (void)_centerHtml->Present(slot);
}

void MenuManager::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (!_core->IsOpen(slot))
            continue;

        MenuRenderer& renderer = RendererFor(slot);
        _core->HandleKeys(slot, renderer.RowsPerPage(),
                          [this](int keyed, int page) { RendererFor(keyed).ShowPage(keyed, page); });

        // Input may have activated a row that closed the menu it was about to draw, or replaced
        // the session with one on the other surface.
        if (!_core->IsOpen(slot))
            continue;

        if (!RendererFor(slot).Present(slot))
            MoveToCenterHtml(slot);
    }
}

}  // namespace VoltMod
