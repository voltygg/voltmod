#pragma once

#include <VoltMod/Core/Capabilities.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuState.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/** Services used by menu sessions and drivers. They must outlive the manager. */
struct MenuServices
{
    VoltMod::Scheduler& Scheduler;
    SlotEvents& Slots;
    EntitySystem& Entities;
    VoltMod::ChatInput& ChatInput;
    VoltMod::Translations& Translations;
    VoltMod::Policy& Policy;
    VoltMod::Messages& Messages;
    CustomUi& Ui;
    VoltMod::Capabilities& Capabilities;
};

/**
 * @brief Stores per-player sessions and draws their menus.
 *
 * Center HTML is the default. A Panorama session is drawn on a panel private to its player, which
 * stays with them through death and spectating; it falls back to center HTML only when that panel
 * cannot be shown. The current stack and cursor survive that switch.
 */
class MenuManager final : public MenuSession
{
public:
    /** Objects referenced by @p services must outlive the manager. */
    explicit MenuManager(const MenuServices& services);
    ~MenuManager() override;

    /**
     * Draw menus into the Panorama layout @p layout from now on.
     *
     * Requires @ref Capability::CustomUi, @ref Capability::UiClicks and @ref Capability::Visibility -
     * the last because each player's menu is an entity only they receive. On success, closes open
     * sessions and switches drivers. On failure, returns an error without changing the driver.
     */
    Status UsePanorama(std::string_view layout = "voltmod_menu");

    /** Draw menus as center HTML from now on, closing every open session first. The default. */
    void UseCenterHtml();

    [[nodiscard]] bool IsPanorama() const noexcept { return !_layout.empty(); }

    /** Panorama layout name, or empty for center HTML. */
    [[nodiscard]] std::string_view Layout() const noexcept { return _layout; }

    /** Start a session for @p slot showing @p menu, closing any session the player already has.
     *  What a command calls; a submenu goes through the two-argument @ref MenuSession::Open. */
    void Open(int slot, std::shared_ptr<Menu> menu, MenuOptions options);

    /** Push @p menu onto the player's session, starting one with default options if none is open. */
    void Open(int slot, std::shared_ptr<Menu> menu) override;
    void Close(int slot) override;
    void CloseAll(int slot) override;
    void CloseAll(int slot, std::string_view replyKey) override;
    void Prompt(int slot, std::string prompt, std::function<bool(int slot, std::string_view text)> callback) override;
    [[nodiscard]] std::string Translate(int slot, std::string_view key, std::string_view fallback) const override;

    [[nodiscard]] bool IsOpen(int slot) const;

    /**
     * Freeze movement for the duration of a session, so navigating does not also walk the player
     * around. The original MoveType is restored when the last menu closes. Disabled by default;
     * turning it off also releases whoever the previous setting had already frozen.
     */
    void FreezeWhileOpen(bool enabled);

private:
    void Push(int slot, std::shared_ptr<Menu> menu);

    [[nodiscard]] MenuDriver& DriverOf(int slot);

    /** Draw @p slot's session as center HTML from now on, because Panorama could not. */
    void FallBack(int slot);

    void OnGameFrame();

    void CloseAllSessions();

    /** Freeze (true) or restore (false) @p pawn's movement; no-op unless freeze is enabled.
     *  Only a live pawn is frozen, and only the pawn that was frozen is restored. The body is a
     *  parameter because the per-frame path already holds it. */
    void SetPlayerFrozen(int slot, bool frozen, const Pawn& pawn);

    void SyncFreeze(int slot, const Pawn& pawn);

    MenuServices _services;
    std::string _layout;
    bool _freezePlayer = false;
    std::unique_ptr<ActiveMenus> _menus;
    std::unique_ptr<MenuDriver> _driver;
    std::unique_ptr<MenuDriver> _fallback;
    /** Declared last: per-frame delivery drops before the state it touches. */
    Subscription _onFrame;
};

}  // namespace VoltMod
