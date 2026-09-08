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

/** Everything a menu session and its drivers reach. All must outlive the manager. */
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
 * @brief The menu service: one per-player session store, and the driver drawing it.
 *
 * A menu is a model - rows and callbacks - and nothing in it says how it reaches a screen. This
 * owns both halves of the answer: the stack, the freeze and the chat prompt live here, while an
 * internal driver does nothing but draw and read input. Plugins never see a driver; they call
 * @ref UsePanorama once at load and open menus the same way either way.
 *
 * Center HTML is the default because it needs nothing: no addon to publish, no capability, any
 * client. @ref UsePanorama upgrades to a clickable `custom_hud_layout` and reports why it could
 * not, which is a plugin's cue to carry on with center HTML rather than draw nothing.
 *
 * A client shows a layout's per-player state for the pawn it is viewing, so a dead or spectating
 * player cannot be reached through Panorama. Such a session is drawn as center HTML until the
 * player is alive again; the stack, cursor and keys carry over.
 *
 * Costs nothing per frame while no menu is open: the frame subscription is taken by the first
 * @ref Open and dropped when the last stack empties.
 */
class MenuManager final : public MenuSession
{
public:
    /** @p services are captured by reference and must outlive the manager; the constructor
     *  subscribes to `Slots`, so it must already be constructed. */
    explicit MenuManager(const MenuServices& services);
    ~MenuManager() override;

    /**
     * Draw menus into the Panorama layout @p layout from now on.
     *
     * Checks @ref Capability::CustomUi and @ref Capability::UiClicks, validates the layout name,
     * closes every open session, then switches. On failure nothing changes and the error says
     * which check failed: @ref ErrorCode::Unsupported names the missing capability and its reason,
     * @ref ErrorCode::Invalid comes from the layout name.
     *
     * Both capabilities are checked because either can be on while the other is off, and a layout
     * that draws but never reports a press is worse than no Panorama menu at all. Nothing is
     * spawned until a menu opens.
     */
    Status UsePanorama(std::string_view layout = "voltmod_menu");

    /** Draw menus as center HTML from now on, closing every open session first. The default. */
    void UseCenterHtml();

    /** True while the Panorama driver is the one drawing. */
    [[nodiscard]] bool IsPanorama() const noexcept;

    /** The layout being drawn into, or empty for center HTML. */
    [[nodiscard]] std::string_view Layout() const noexcept;

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

    /** True if the player has any menu currently open. */
    [[nodiscard]] bool IsOpen(int slot) const;

    /**
     * Freeze movement for the duration of a session, so navigating does not also walk the player
     * around. The original MoveType is restored when the last menu closes. Disabled by default;
     * turning it off also releases whoever the previous setting had already frozen.
     */
    void FreezeWhileOpen(bool enabled);

private:
    /** Put @p menu on top of @p slot's stack and start drawing it. */
    void Push(int slot, std::shared_ptr<Menu> menu);

    /** The driver drawing @p slot's session: the chosen one, or the fallback while
     *  @ref PlayerMenuState::OnFallback. */
    [[nodiscard]] MenuDriver& DriverOf(int slot);

    /** Switch @p slot between the chosen driver and the fallback as the player dies and spawns,
     *  dismissing the old driver's drawing first. @p pawn is the slot's body this frame. */
    void SyncDriver(int slot, const Pawn& pawn);

    /** Drive @p slot's open menu for one frame, and stop paying per frame once none are left. */
    void OnGameFrame();

    /** Close every open session, so a driver swap leaves nothing half-drawn on the old one. */
    void CloseAllSessions();

    /** Freeze (true) or restore (false) @p pawn's movement; no-op unless freeze is enabled.
     *  Only a live pawn is frozen, and only the pawn that was frozen is restored. The body is a
     *  parameter because the per-frame path already holds it. */
    void SetPlayerFrozen(int slot, bool frozen, const Pawn& pawn);

    /** Per frame: drop the hold on a pawn that died or was replaced, and freeze the live one. */
    void SyncFreeze(int slot, const Pawn& pawn);

    MenuServices _services;
    /** The layout the Panorama driver was asked for; empty while center HTML is drawing. */
    std::string _layout;
    bool _freezePlayer = false;
    /** The stacks, cursors and rows every driver reads. Behind a pointer because it is defined
     *  under src/, which is what keeps a driver out of this header. */
    std::unique_ptr<OpenMenus> _menus;
    /** The keys both drivers read. */
    std::unique_ptr<MenuKeys> _keys;
    /** Behind a pointer so no public header reaches a driver, and so a swap is one assignment. */
    std::unique_ptr<MenuDriver> _driver;
    /** Center HTML for sessions whose player is not alive; only while Panorama is chosen. */
    std::unique_ptr<MenuDriver> _fallback;
    /** Declared last: per-frame delivery drops before the state it touches. */
    Subscription _onFrame;
};

}  // namespace VoltMod
