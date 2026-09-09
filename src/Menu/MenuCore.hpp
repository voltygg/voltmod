#pragma once

#include "Menu/ActiveMenus.hpp"

#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Menu/MenuState.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Session plumbing shared by the menu hosts: stacks, keys, the freeze, and per-frame work.
 *
 * A host owns one of these and adds the drawing. Nothing here draws anything, so center HTML and
 * Panorama keep one set of stacks, cursors and keys between them.
 */
class MenuCore
{
public:
    /** What @ref Close and @ref CloseAll left behind, so a host knows whether to redraw or dismiss. */
    enum class CloseResult
    {
        Stayed,   ///< A parent menu is now showing.
        Emptied,  ///< The stack is empty; take the menu off the screen.
        NotOpen   ///< The player had no menu.
    };

    /** @p services and @p host must outlive the core. @p host is the session row callbacks are
     *  handed, and the one keys close menus through, so its drawing stays in step. */
    MenuCore(const MenuServices& services, MenuSession& host);
    ~MenuCore();

    MenuCore(const MenuCore&) = delete;
    MenuCore& operator=(const MenuCore&) = delete;

    [[nodiscard]] ActiveMenus& Menus() { return *_menus; }
    [[nodiscard]] const MenuServices& Services() const noexcept { return _services; }

    /** Start a session for @p slot showing @p menu, replacing any the player has open. */
    void Start(int slot, std::shared_ptr<Menu> menu, MenuOptions options);

    /** Push @p menu onto an open session and arm the per-frame work. */
    void Push(int slot, std::shared_ptr<Menu> menu);

    CloseResult Close(int slot);
    CloseResult CloseAll(int slot);

    /** Send @p replyKey to @p slot, translated. Called before closing, while the addressee still
     *  has the menu the reply answers. */
    void Reply(int slot, std::string_view replyKey);

    void Prompt(int slot, std::string prompt, std::function<bool(int slot, std::string_view text)> callback);
    void CancelPrompt(int slot);

    [[nodiscard]] std::string Translate(int slot, std::string_view key, std::string_view fallback) const;

    [[nodiscard]] bool IsOpen(int slot) const { return _menus->IsOpen(slot); }
    [[nodiscard]] bool AnyOpen() const { return _menus->AnyOpen(); }

    /** Freeze movement for the duration of a session; turning it off releases whoever the previous
     *  setting had already frozen. */
    void FreezeWhileOpen(bool enabled);

    /**
     * Handle the W/S/A/D/E/R controls for @p slot, debounced by 200 ms. @p showPage is called with
     * the page the cursor moved onto, and may be empty for a host that has no pages of its own.
     * False when the session has keys off or nothing was consumed.
     */
    bool HandleKeys(int slot, int rowsPerPage, const std::function<void(int slot, int page)>& showPage);

    /** The host's per-frame work, run while any session is open. Set once. */
    void EveryFrame(std::function<void()> work);

private:
    static constexpr int64_t InputDebounceMs = 200;

    void OnGameFrame();

    bool HandlePressed(int slot, uint64_t pressed, int rowsPerPage, const std::function<void(int, int)>& showPage);
    void MoveCursor(int slot, int step, int rowsPerPage, const std::function<void(int, int)>& showPage);
    void JumpPage(int slot, int delta, int rowsPerPage, const std::function<void(int, int)>& showPage);

    /** Freeze (true) or restore (false) @p pawn's movement; no-op unless freeze is enabled.
     *  Only a live pawn is frozen, and only the pawn that was frozen is restored. The body is a
     *  parameter because the per-frame path already holds it. */
    void SetPlayerFrozen(int slot, bool frozen, const Pawn& pawn);

    void SyncFreeze(int slot, const Pawn& pawn);

    MenuServices _services;
    MenuSession& _host;
    bool _freezePlayer = false;
    std::unique_ptr<ActiveMenus> _menus;
    std::function<void()> _work;
    /** Declared last: per-frame delivery drops before the state it touches. */
    Subscription _onFrame;
};

}  // namespace VoltMod
