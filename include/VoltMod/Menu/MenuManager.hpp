#pragma once

#include <VoltMod/Core/Capabilities.hpp>
#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/MoveType.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <functional>
#include <memory>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

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
 * @brief Options for the session an @ref MenuManager::Open call starts.
 *
 * A session runs from the first menu opened for a player until their stack empties, so these apply
 * only to the call that opens the stack; a submenu pushed later inherits the live session.
 */
struct MenuOptions
{
    /** Whether the global movement freeze (@ref MenuManager::FreezeWhileOpen) applies. Pass false
     *  for menus players reach mid-round, where being held still is worse than stray movement. */
    bool FreezeMovement = true;
};

/** One player's session: the stack of open menus and the freeze taken out for it. */
struct PlayerMenuState
{
    /** The stack of menus currently open for the player. */
    std::stack<std::shared_ptr<Menu>> MenuStack;

    /** True while the manager is holding the player's movement frozen for this session. */
    bool MovementFrozen = false;
    /** MoveType captured before freezing, restored when the menu closes. */
    MoveType PrevMoveType = MoveType::Walk;

    /** True if the player has any menu currently open. */
    bool HasMenu() const { return !MenuStack.empty(); }
    /** Top of the stack, or nullptr if no menu is open. */
    Menu* GetCurrentMenu() { return MenuStack.empty() ? nullptr : MenuStack.top().get(); }

    /** Clears the entire menu stack and the freeze bookkeeping. */
    void Reset() { *this = {}; }
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

    /** Push @p menu onto the player's stack and start showing it. @p options take effect only
     *  when this call opens the stack (see @ref MenuOptions). */
    void Open(int slot, std::shared_ptr<Menu> menu, MenuOptions options);

    void Open(int slot, std::shared_ptr<Menu> menu) override;
    void Close(int slot) override;
    void CloseAll(int slot) override;
    void CloseAll(int slot, std::string_view replyKey) override;
    void Prompt(int slot, std::string prompt, std::function<bool(int slot, std::string_view text)> callback) override;

    /** True if the player has any menu currently open. */
    [[nodiscard]] bool IsOpen(int slot) const;

    /**
     * Freeze movement for the duration of a session, so navigating does not also walk the player
     * around. The original MoveType is restored when the last menu closes. Disabled by default;
     * turning it off also releases whoever the previous setting had already frozen.
     */
    void FreezeWhileOpen(bool enabled);

private:
    // The drivers reach the session state through MenuDriver's protected helpers, which is the
    // whole of what they may touch here.
    friend class MenuDriver;

    /** Run row @p index of the player's current menu, as if it had been selected and confirmed.
     *  Ignores rows that are disabled, unselectable, or out of range. */
    void Activate(int slot, int index);

    /** Nudge row @p index's value by @p direction (-1 or +1). True when the row consumed it,
     *  which is what tells a keyboard driver to page instead. */
    bool Step(int slot, int index, int direction);

    /** True when the cursor is allowed to land on @p item, as it describes itself to @p slot. */
    static bool IsCursorTarget(const MenuItem& item, int slot);

    /** Move @p idx by @p step, wrapping over @p items and skipping rows the cursor cannot land
     *  on for @p slot. */
    static void StepCursor(int slot, const std::vector<MenuItem>& items, int& idx, int step);

    /** Put @p index on the first row the cursor may land on, so a disabled or Text row is never
     *  the initial selection. */
    void SelectFirst(int slot, int& index);

    /** Read the top of @p slot's stack. Null when nothing is open. */
    [[nodiscard]] Menu* Current(int slot);

    /** How many menus are stacked for @p slot. */
    [[nodiscard]] int Depth(int slot) const;

    /** Drive @p slot's open menu for one frame, and stop paying per frame once none are left. */
    void OnGameFrame();

    /** True while any slot has a menu open. */
    [[nodiscard]] bool AnyOpen() const;

    /** Close every open session, so a driver swap leaves nothing half-drawn on the old one. */
    void CloseAllSessions();

    /** Freeze (true) or restore (false) the player's movement; no-op unless freeze is enabled. */
    void SetPlayerFrozen(int slot, bool frozen);

    MenuServices _services;
    /** Per-player sessions; PerSlot clears a slot's stack when it changes hands. */
    PerSlot<PlayerMenuState> _states;
    /** The layout the Panorama driver was asked for; empty while center HTML is drawing. */
    std::string _layout;
    bool _freezePlayer = false;
    /** Behind a pointer so no public header reaches a driver, and so a swap is one assignment. */
    std::unique_ptr<MenuDriver> _driver;
    /** Declared last: per-frame delivery drops before the state it touches. */
    Subscription _onFrame;
};

}  // namespace VoltMod
