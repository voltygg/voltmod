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
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuState.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <cstdint>
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
    // whole of what they may touch here; MenuKeys is the shared half of a driver's HandleInput
    // and reaches the same session directly.
    friend class MenuDriver;
    friend class MenuKeys;

    /** Run row @p index of the player's current menu, as if it had been selected and confirmed.
     *  Ignores rows that are disabled, unselectable, or out of range.
     *
     *  Runs a commit held for another row first, and cancels one held for *this* row: a row
     *  whose activation is its own commit would otherwise apply the value twice. */
    void Activate(int slot, int index);

    /** Nudge row @p index's value by @p direction (-1 or +1). True when the row consumed it,
     *  which is what tells a keyboard driver to page instead.
     *
     *  A row with a @ref MenuItem::Commit is stepped, not applied: the commit is held for a
     *  moment so a burst of presses runs one action. */
    bool Step(int slot, int index, int direction);

    /** Row @p index as it describes itself to @p slot, with @ref MenuRow::Pending and
     *  @ref MenuRow::Changed filled in. What a driver draws from; an index with no row behind it
     *  describes as an inert, unselectable line. */
    [[nodiscard]] MenuRow Describe(int slot, int index);

    /** Where @p slot's cursor is. */
    [[nodiscard]] int Selected(int slot) const;

    /** Put @p slot's cursor on row @p index, applying whatever the row it leaves was holding. */
    void Select(int slot, int index);

    /** Put @p slot's cursor on the first row it may land on within @p page of @p rowsPerPage,
     *  for a driver whose page turned without the cursor. */
    void SelectOnPage(int slot, int page, int rowsPerPage);

    /** The titles under the current menu, joined with ` > `; empty at the root. */
    [[nodiscard]] std::string Crumbs(int slot) const;

    /** Whether keys drive @p slot's session (@ref MenuOptions::Keyboard). */
    [[nodiscard]] bool KeyboardEnabled(int slot) const;

    /** Read @p slot's keys and act on them for @p driver's page shape. True when one was
     *  consumed. Both drivers' @ref MenuDriver::HandleInput is this call. */
    bool HandleKeys(int slot, MenuDriver& driver);

    /** Start the cursor, the debounce window and the row memory over, because @p slot's top menu
     *  changed. */
    void ResetCursor(int slot);

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

    /** How long a row's value may sit on screen marked as just changed. */
    static constexpr int64_t ChangedMs = 150;

    MenuServices _services;
    /** Per-player sessions; PerSlot clears a slot's stack when it changes hands. */
    PerSlot<PlayerMenuState> _states;
    /** The layout the Panorama driver was asked for; empty while center HTML is drawing. */
    std::string _layout;
    bool _freezePlayer = false;
    /** Commits held back while a row is being stepped. Behind a pointer for the same reason as
     *  @ref _driver: it is defined under src/. */
    std::unique_ptr<PendingCommit> _pending;
    /** Where every player's cursor is, and the moves over a menu's rows. Behind a pointer for
     *  the same reason as @ref _pending: it is defined under src/. */
    std::unique_ptr<MenuCursor> _cursor;
    /** The keys both drivers read. */
    std::unique_ptr<MenuKeys> _keys;
    /** Behind a pointer so no public header reaches a driver, and so a swap is one assignment. */
    std::unique_ptr<MenuDriver> _driver;
    /** Declared last: per-frame delivery drops before the state it touches. */
    Subscription _onFrame;
};

}  // namespace VoltMod
