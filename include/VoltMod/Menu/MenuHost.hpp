#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Players/ActionDispatcher.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief Options for the menu session an @ref MenuHost::OpenMenu call starts.
 *
 * A session runs from the first menu opened for a player until their stack empties, so these apply
 * only to the call that opens the stack; a submenu pushed later inherits the live session.
 */
struct MenuSessionOptions
{
    /** Whether the global movement freeze (@ref MenuHost::SetFreezePlayer) applies. Pass false
     *  for menus players reach mid-round, where being held still is worse than stray movement. */
    bool FreezeMovement = true;
};

/**
 * @brief Whatever a @ref MenuView is opened on: the session, the stack, and the row context.
 *
 * A menu is a model - rows and callbacks - and nothing in it says how it reaches a screen, so
 * everything that runs a menu *session* lives here and each subclass adds only how it draws:
 * @ref Present, @ref Dismiss, and the input that calls @ref Activate. That is what lets
 * @ref MenuBuilder and @ref Flow build one menu that either driver can show.
 *
 * @ref HtmlMenuManager renders center HTML and reads WASD; @ref UiMenuManager drives a Panorama
 * layout and reads clicks. A plugin picks one and passes it wherever a `MenuHost&` is asked for.
 */
class MenuHost
{
public:
    virtual ~MenuHost() = default;

    MenuHost(const MenuHost&) = delete;
    MenuHost& operator=(const MenuHost&) = delete;

    /** Push @p menu onto the player's stack and start showing it. @p options take effect only
     *  when this call opens the stack (see @ref MenuSessionOptions). */
    void OpenMenu(int slot, std::shared_ptr<MenuView> menu, MenuSessionOptions options = {});

    /** Pop the top menu, falling back to the parent if one exists. */
    void CloseMenu(int slot);

    /** Clear the entire stack and take the menu off the player's screen. */
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
     * Freeze movement for the duration of a menu session, so navigating does not also walk the
     * player around. The original MoveType is restored when the last menu closes. Disabled by
     * default; turning it off also releases whoever the previous setting had already frozen.
     */
    void SetFreezePlayer(bool enabled);

protected:
    /** Takes exactly the services a menu session and its context rows use; all must outlive this
     *  object. Subscribes to @p slots, so it must already be constructed. */
    MenuHost(SlotEvents& slots, EntitySystem& entities, ChatInput& chatInput, Translations& translations,
             Policy& policy, PlayerManager& players);

    /** Draw @p slot's current menu. Called by the subclass, per tick or per change. */
    virtual void Present(int slot) = 0;

    /** @p slot has no menu any more: take whatever this driver put on their screen off it. */
    virtual void Dismiss(int slot) = 0;

    /** Run row @p index of the player's current menu, as if it had been selected and confirmed.
     *  Ignores rows that are disabled, unselectable, or out of range. */
    void Activate(int slot, int index);

    /** Nudge row @p index's value by @p direction (-1 or +1). True when the row consumed it,
     *  which is what tells a keyboard driver to page instead. */
    bool Step(int slot, int index, int direction);

    /** True when the cursor is allowed to land on @p option. */
    static bool IsCursorTarget(const std::shared_ptr<MenuOption>& option);

    /** Move @p idx by @p step, wrapping over @p items and skipping rows the cursor cannot land on. */
    static void StepCursor(const std::vector<std::shared_ptr<MenuOption>>& items, int& idx, int step);

    /** Freeze (true) or restore (false) the player's movement; no-op unless freeze is enabled. */
    void SetPlayerFrozen(int slot, bool frozen);

    EntitySystem& _entities;
    ChatInput& _chatInput;
    Translations& _translations;
    Policy& _policy;
    PlayerManager& _players;
    ActionDispatcher _actions;
    /** Per-player menu state; PerSlot clears a slot's stack when it changes hands. */
    PerSlot<PlayerMenuState> _states;
    bool _freezePlayer = false;

private:
    /** The row context below is the builder's alone: a plugin reaches these services through its
     *  own Runtime, and @ref _actions is this host's composition detail, not a public seam. */
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

    /** Put the cursor on the first row it may land on, so a disabled or Text row is never the
     *  initial selection. */
    void SelectFirst(int slot);
};

}  // namespace VoltMod
