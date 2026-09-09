#pragma once

#include <VoltMod/Core/Capabilities.hpp>
#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuState.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <VoltMod/Workshop/Addons.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/** Services used by menu sessions. They must outlive the manager. */
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
    VoltMod::Addons& Addons;
};

/** How the Panorama menu is drawn for players who can see it. */
struct PanoramaMenuOptions
{
    int Rows = 8;                              ///< Rows per page; the layout holds 10.
    int Nav = 0;                               ///< Tabs over the root menu's submenu rows; the layout holds 8.
    std::string_view Layout = "voltmod_menu";  ///< A layout carrying the framework menu's ids.
};

/**
 * @brief Stores per-player sessions and draws each on the surface its player can read.
 *
 * Center HTML needs no client addon and is read with the keyboard, so it is what everyone gets.
 * After @ref UsePanorama, a player who has the menu layout and a cursor gets a clickable Panorama
 * panel instead - the same sessions, rows and callbacks either way. A session survives death and
 * spectating.
 */
class MenuManager final : public MenuSession
{
public:
    /** Objects referenced by @p services must outlive the manager. */
    explicit MenuManager(const MenuServices& services);
    ~MenuManager() override;

    /**
     * Draw on Panorama from now on for every player who can see it: @ref Capability::CustomUi,
     * @ref Capability::UiClicks and @ref Capability::Visibility on, the menu addon downloaded, and
     * a private panel that spawns. Everyone else keeps center HTML. Sessions already open keep
     * the surface they were opened on.
     */
    void UsePanorama(PanoramaMenuOptions options);

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

    /** Whether @p slot's open session is drawn on Panorama. False when nothing is open. */
    [[nodiscard]] bool IsPanorama(int slot) const;

    /**
     * Freeze movement for the duration of a session, so navigating does not also walk the player
     * around. The original MoveType is restored when the last menu closes. Disabled by default;
     * turning it off also releases whoever the previous setting had already frozen.
     */
    void FreezeWhileOpen(bool enabled);

private:
    /** The surface @p slot's session is drawn on. */
    [[nodiscard]] MenuRenderer& RendererFor(int slot);

    /** Whether a session starting now for @p slot would be drawn on Panorama. */
    [[nodiscard]] bool CanUsePanorama(int slot) const;

    /** Move @p slot's open session to center HTML, for a Panorama panel that has gone. */
    void MoveToCenterHtml(int slot);

    void OnGameFrame();

    MenuServices _services;
    /** Which surface each open session is drawn on; reset with the slot. */
    PerSlot<bool> _onPanorama;
    /** Held by pointer: the renderers are internal to the framework. Panorama is null until
     *  @ref UsePanorama asks for it. */
    std::unique_ptr<MenuRenderer> _centerHtml;
    std::unique_ptr<MenuRenderer> _panorama;
    /** Held by pointer: MenuCore is internal to the framework. Declared last so per-frame
     *  delivery drops before the state it touches. */
    std::unique_ptr<MenuCore> _core;
};

}  // namespace VoltMod
