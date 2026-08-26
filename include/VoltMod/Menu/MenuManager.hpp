#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Options for the menu session an @ref MenuManager::OpenMenu call starts.
 *
 * A session runs from the first menu opened for a player until their stack empties, so these apply
 * only to the call that opens the stack; a submenu pushed later inherits the live session.
 */
struct MenuSessionOptions
{
    /** Whether the global movement freeze (@ref MenuManager::SetFreezePlayer) applies. Pass false
     *  for menus players reach mid-round, where being held still is worse than stray WASD movement. */
    bool FreezeMovement = true;
};

/**
 * @brief WASD-navigated center-HTML menus for all players.
 * Supports a per-player menu stack (submenus push, R pops back).
 * Reads button state each tick from a scheduler pump it registers for itself.
 */
class MenuManager
{
public:
    /**
     * @param runtime supplies the frame pump, the slot feed, the entity reads, the chat-input
     *        capture, the translations and the reply policy. It must outlive the manager, which
     *        the runtime's own declaration order guarantees. The constructor subscribes to
     *        `runtime.Scheduler` and `runtime.Slots`, so both must already be constructed.
     */
    explicit MenuManager(Runtime& runtime);

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
    /** Per-tick driver: reads buttons, advances selection, and re-renders. */
    void OnGameFrame();

    void HandleInput(int slot, uint64_t buttons, uint64_t prevButtons);
    void RenderMenu(int slot);

    /** Freeze (true) or restore (false) the player's movement; no-op unless freeze is enabled. */
    void SetPlayerFrozen(int slot, bool frozen);

    Runtime& _runtime;
    /** Per-player menu state; PerSlot clears a slot's stack when it changes hands. */
    PerSlot<PlayerMenuState> _states;
    static constexpr int64_t InputDebounceMs = 200;
    bool _freezePlayer = false;
    /** Declared after _states: the frame pump drops before the state it touches. */
    Subscription _pump;
};

}  // namespace VoltMod
