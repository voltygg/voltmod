#include <VoltMod/Menu/MenuRouter.hpp>
#include <utility>

namespace VoltMod
{

MenuRouter::MenuRouter(MenuSurface& fallback) : _fallback(fallback) {}

Subscription MenuRouter::Prefer(MenuSurface& surface)
{
    _preferred = &surface;
    return Subscription([this, &surface] {
        // A later Prefer replaced this one; leave that in place.
        if (_preferred == &surface)
        {
            _preferred = nullptr;
        }
    });
}

bool MenuRouter::OpenSession(int slot, std::shared_ptr<Menu> menu, MenuOptions options)
{
    // One session per player: a menu left on the other surface would stay open and frozen.
    if (_preferred)
    {
        _preferred->CloseAll(slot);
    }
    _fallback.CloseAll(slot);

    if (_preferred && _preferred->OpenSession(slot, menu, options))
    {
        return true;
    }
    return _fallback.OpenSession(slot, std::move(menu), options);
}

void MenuRouter::Open(int slot, std::shared_ptr<Menu> menu)
{
    if (!IsOpen(slot))
    {
        OpenSession(slot, std::move(menu), {});
        return;
    }

    SessionOf(slot).Open(slot, std::move(menu));
}

bool MenuRouter::IsOpen(int slot) const
{
    return (_preferred && _preferred->IsOpen(slot)) || _fallback.IsOpen(slot);
}

void MenuRouter::Close(int slot)
{
    SessionOf(slot).Close(slot);
}

void MenuRouter::CloseAll(int slot)
{
    SessionOf(slot).CloseAll(slot);
}

void MenuRouter::CloseAll(int slot, std::string_view replyKey)
{
    SessionOf(slot).CloseAll(slot, replyKey);
}

void MenuRouter::Prompt(int slot, std::string prompt, std::function<bool(int, std::string_view)> callback)
{
    SessionOf(slot).Prompt(slot, std::move(prompt), std::move(callback));
}

std::string MenuRouter::Translate(int slot, std::string_view key, std::string_view fallback) const
{
    return SessionOf(slot).Translate(slot, key, fallback);
}

MenuSurface& MenuRouter::SessionOf(int slot) const
{
    return _preferred && _preferred->IsOpen(slot) ? *_preferred : _fallback;
}

}  // namespace VoltMod
