#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuFreeze.hpp>
#include <VoltMod/Menu/MenuStack.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The menu surface every player has: their open menus drawn as center HTML.
 *
 * Center HTML needs no client addon and is read with W/S/A/D/E/R, so every player can be drawn
 * to. A session survives death and spectating.
 *
 * A plugin that wants a clickable menu builds its own Panorama screen on @ref Screen and the
 * block library instead; the framework ships the pieces, not a fixed menu layout.
 */
class CenterHtmlMenu final : public MenuSurface
{
public:
    /** The services a center-HTML menu draws and listens through. All must outlive this. */
    struct Services
    {
        VoltMod::Scheduler& Scheduler;
        SlotEvents& Slots;
        EntitySystem& Entities;
        VoltMod::MenuFreeze& Freeze;
        VoltMod::ChatInput& ChatInput;
        VoltMod::Translations& Translations;
        VoltMod::Policy& Policy;
        VoltMod::Messages& Messages;
    };

    explicit CenterHtmlMenu(const Services& services);

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

private:
    /** Presses closer together than this are ignored. */
    static constexpr int64_t PressGapMs = 200;

    /** Where one player is in the menu on top: the selected row, the buttons held last frame for
     *  edge detection, and when the last press was acted on. */
    struct Cursor
    {
        int Selected = 0;
        uint64_t PrevButtons = 0;
        int64_t LastInputTime = 0;
    };

    /** Push @p menu onto an open session and start the per-frame work. */
    void Push(int slot, std::shared_ptr<Menu> menu);

    /** Put the cursor back on the first selectable row of whatever is now on top. */
    void ResetCursor(int slot);

    /** Put @p slot's cursor on row @p index, applying whatever the row it leaves was holding. An
     *  index the current menu does not have is dropped. */
    void Select(int slot, int index);

    /** Send the player's current menu, or its pending chat prompt, as center HTML. */
    void Draw(int slot);

    void OnGameFrame();

    /** The W/S/A/D/E/R controls for @p slot. True when a press was consumed. */
    bool ReadKeys(int slot);
    bool RunKey(int slot, uint64_t pressed);
    void MoveCursor(int slot, int step);
    void JumpPage(int slot, int delta);

    Services _services;
    /** The half every menu surface shares: stack, breadcrumb, Describe, Activate and Step. */
    MenuStack _stack;
    PerSlot<Cursor> _cursors;
    /** Declared last: per-frame delivery drops before the state it touches. */
    Subscription _onFrame;
};

}  // namespace VoltMod
