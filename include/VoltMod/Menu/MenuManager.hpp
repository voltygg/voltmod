#pragma once

#include <VoltMod/Core/Capabilities.hpp>
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
};

/**
 * @brief Stores per-player sessions and draws them as center HTML.
 *
 * Center HTML needs no client addon and is read with the keyboard. A session survives death and
 * spectating.
 */
class MenuManager final : public MenuSession
{
public:
    /** Objects referenced by @p services must outlive the manager. */
    explicit MenuManager(const MenuServices& services);
    ~MenuManager() override;

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

    /** The services this manager was built with, for another menu host built beside it. */
    [[nodiscard]] const MenuServices& Services() const noexcept { return _services; }

private:
    /** Draw the menu at the top of @p slot's stack. */
    void Present(int slot);

    /** Take @p slot's menu off the screen. */
    void Dismiss(int slot);

    void OnGameFrame();

    MenuServices _services;
    /** Held by pointer: MenuCore is internal to the framework. Declared last so per-frame
     *  delivery drops before the state it touches. */
    std::unique_ptr<MenuCore> _core;
};

}  // namespace VoltMod
