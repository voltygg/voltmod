#pragma once

#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief `runtime.Menus`: the surface menus start on, chosen once per session.
 *
 * A session starts on the preferred surface when it can draw for the player, and on the fallback
 * otherwise. Every later call follows the surface holding that player's session, and a player has
 * one session across both. Rows and flows are built against this, so they never pick a surface
 * themselves. SDK-free.
 */
class MenuRouter final : public MenuSurface
{
public:
    /** @p fallback takes every session no preferred surface does, and must outlive this. */
    explicit MenuRouter(MenuSurface& fallback);

    /** Try @p surface first for new sessions while the returned Subscription is held; hold it no
     *  longer than @p surface lives. A later call replaces the preference. */
    [[nodiscard]] Subscription Prefer(MenuSurface& surface);

    bool OpenSession(int slot, std::shared_ptr<Menu> menu, MenuOptions options) override;
    void Open(int slot, std::shared_ptr<Menu> menu) override;
    [[nodiscard]] bool IsOpen(int slot) const override;
    void Close(int slot) override;
    void CloseAll(int slot) override;
    void CloseAll(int slot, std::string_view replyKey) override;
    void Prompt(int slot, std::string prompt, std::function<bool(int slot, std::string_view text)> callback) override;
    [[nodiscard]] std::string Translate(int slot, std::string_view key, std::string_view fallback) const override;

private:
    /** The surface holding @p slot's session; the fallback when there is none. */
    [[nodiscard]] MenuSurface& SessionOf(int slot) const;

    MenuSurface& _fallback;
    MenuSurface* _preferred = nullptr;
};

}  // namespace VoltMod
