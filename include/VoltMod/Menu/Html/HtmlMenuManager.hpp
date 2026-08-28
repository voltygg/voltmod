#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Options for the menu session an @ref HtmlMenuManager::OpenMenu call starts.
 *
 * A session runs from the first menu opened for a player until their stack empties, so these apply
 * only to the call that opens the stack; a submenu pushed later inherits the live session.
 */
struct MenuSessionOptions
{
    /** Whether the global movement freeze (@ref HtmlMenuManager::SetFreezePlayer) applies. Pass false
     *  for menus players reach mid-round, where being held still is worse than stray WASD movement. */
    bool FreezeMovement = true;
};

/**
 * @brief WASD-navigated center-HTML menus for all players.
 * Supports a per-player menu stack (submenus push, R pops back).
 * Reads button state each tick from a scheduler pump it registers for itself.
 */
class HtmlMenuManager
{
public:
    /**
     * Takes exactly the services menu dispatch and context rows use. All must outlive the
     * manager. The constructor subscribes to @p scheduler and @p slots, so both must already be
     * constructed - true whenever a Runtime builds this in declaration order.
     */
    HtmlMenuManager(Scheduler& scheduler, SlotEvents& slots, EntitySystem& entities, Messages& messages,
                    ChatInput& chatInput, Translations& translations, Policy& policy, PlayerManager& players);

    HtmlMenuManager(const HtmlMenuManager&) = delete;
    HtmlMenuManager& operator=(const HtmlMenuManager&) = delete;

    /** Push @p menu onto the player's stack and start rendering it. @p options take effect only
     *  when this call opens the stack (see @ref MenuSessionOptions). */
    void OpenMenu(int slot, std::shared_ptr<MenuView> menu, MenuSessionOptions options = {});

    /** Pop the top menu (firing its OnClose); falls back to the parent if one exists. */
    void CloseMenu(int slot);

    /** Clear the entire stack and hide the HUD for the player. */
    void CloseAllMenus(int slot);

    /** Abort a player's menus with an explanation: @p key is translated in their language and
     *  sent through `Policy.Reply` (skipped when no reply policy is installed), then every
     *  menu closes. The abort path for flows whose preconditions stopped holding. */
    void CloseAllWithReply(int slot, std::string_view key);

    /** Route the player's next chat line to @p callback, showing @p prompt over the open menu.
     *  Rows use this instead of reaching for the runtime's ChatInput themselves. */
    void BeginInput(int slot, std::string prompt, ChatInput::Callback callback);

    /** True if the player has any menu currently open. */
    bool HasActiveMenu(int slot) const;

    /**
     * @brief When enabled, the player's movement is frozen for as long as a menu is open,
     * so WASD navigation does not also walk the player around. The original MoveType is
     * restored when the last menu closes. Disabled by default.
     */
    /** Freeze movement for the duration of a menu session. Turning this off also releases
     *  anyone the previous setting had already frozen. */
    void SetFreezePlayer(bool enabled);

private:
    /** The row context below is the builder's alone: a plugin reaches these services through its
     *  own Runtime, and @ref _actions is this manager's composition detail, not a public seam. */
    friend class MenuBuilder;

    /** The dispatcher context rows (@ref MenuBuilder::Row and friends) run actions through - a
     *  long-lived instance so a row press needs no throwaway dispatcher. */
    ActionDispatcher& Actions() { return _actions; }

    /** For context rows that need to read a player (e.g. the effect-picker submenu title). */
    PlayerManager& Players() { return _players; }

    EntitySystem& Entities() { return _entities; }

    /** For context rows to re-check a permission without running an action (row enabled state). */
    Policy& AccessPolicy() { return _policy; }

    /** For context rows to translate a label key in the viewing player's language. */
    Translations& Translation() { return _translations; }

    /** Per-tick driver: reads buttons, advances selection, and re-renders. */
    void OnGameFrame();

    void HandleInput(int slot, uint64_t buttons, uint64_t prevButtons);
    void RenderMenu(int slot);

    /** Freeze (true) or restore (false) the player's movement; no-op unless freeze is enabled. */
    void SetPlayerFrozen(int slot, bool frozen);

    EntitySystem& _entities;
    Messages& _messages;
    ChatInput& _chatInput;
    Translations& _translations;
    Policy& _policy;
    PlayerManager& _players;
    ActionDispatcher _actions;
    /** Per-player menu state; PerSlot clears a slot's stack when it changes hands. */
    PerSlot<PlayerMenuState> _states;
    static constexpr int64_t InputDebounceMs = 200;
    bool _freezePlayer = false;
    /** Declared after _states: the frame pump drops before the state it touches. */
    Subscription _pump;
};

}  // namespace VoltMod
