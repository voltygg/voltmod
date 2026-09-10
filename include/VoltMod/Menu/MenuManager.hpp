#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/MovementFreeze.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuState.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <cstdint>
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
};

/**
 * @brief Per-player menu sessions, drawn as center HTML.
 *
 * Center HTML needs no client addon and is read with W/S/A/D/E/R, so every player can be drawn
 * to. A session survives death and spectating.
 *
 * A plugin that wants a clickable menu builds its own Panorama screen on @ref Screen and the
 * block library instead; the framework ships the pieces, not a fixed menu layout.
 */
class MenuManager final : public MenuSurface
{
public:
    /** Objects referenced by @p services must outlive the manager. */
    explicit MenuManager(const MenuServices& services);
    ~MenuManager() override;

    /** Start a session for @p slot showing @p menu, closing any session the player already has.
     *  What a command calls; a submenu goes through the one-argument @ref MenuSurface::Open. */
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
    static constexpr int64_t InputDebounceMs = 200;

    /** Push @p menu onto an open session and arm the per-frame work. */
    void Push(int slot, std::shared_ptr<Menu> menu);

    /** Send the player's current menu, or its pending chat prompt, as center HTML. */
    void Present(int slot);

    void OnGameFrame();

    /** The W/S/A/D/E/R controls for @p slot, debounced. True when a press was consumed. */
    bool HandleKeys(int slot);
    bool HandlePressed(int slot, uint64_t pressed);
    void MoveCursor(int slot, int step);
    void JumpPage(int slot, int delta);

    /** Freeze (true) or restore (false) @p pawn's movement; no-op unless freeze is enabled.
     *  Only a live pawn is frozen, and only the pawn that was frozen is restored. The body is a
     *  parameter because the per-frame path already holds it. */
    void SetPlayerFrozen(int slot, bool frozen, const Pawn& pawn);

    void SyncFreeze(int slot, const Pawn& pawn);

    /** What one session asked for, and the pawn it is actually holding. */
    struct SessionFreeze
    {
        bool Wanted = true;
        MovementFreeze Held;
    };

    MenuServices _services;
    bool _freezePlayer = false;
    PerSlot<SessionFreeze> _freezes;
    /** Held by pointer: ActiveMenus is internal to the framework. */
    std::unique_ptr<ActiveMenus> _menus;
    /** Declared last: per-frame delivery drops before the state it touches. */
    Subscription _onFrame;
};

}  // namespace VoltMod
