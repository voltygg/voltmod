#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/Subscriptions.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuFreeze.hpp>
#include <VoltMod/Menu/MenuLayout.hpp>
#include <VoltMod/Menu/MenuStack.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Ui/ButtonPress.hpp>
#include <VoltMod/Ui/ScreenManager.hpp>
#include <VoltMod/Workshop/Addons.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief Menu sessions drawn on a plugin's Panorama @ref MenuLayout and driven by clicks.
 *
 * Holds a @ref MenuStack like @ref CenterHtmlMenu does, so the same rows behave the same on both.
 * The root menu's submenus become sidebar tabs. Create one only when the plugin draws on Panorama
 * and hand it to `runtime.Menus.Prefer`, so sessions start here for players who can see the layout.
 */
class PanoramaMenu final : public MenuSurface
{
public:
    /** The services a Panorama menu draws and listens through. All must outlive this. */
    struct Services
    {
        VoltMod::Scheduler& Scheduler;
        SlotEvents& Slots;
        VoltMod::MenuFreeze& Freeze;
        VoltMod::ChatInput& ChatInput;
        VoltMod::Translations& Translations;
        VoltMod::Policy& Policy;
        ScreenManager& Screens;
        VoltMod::Addons& Addons;
    };

    /** @p layout must outlive this. @p addonId is the workshop addon shipping the layout, required of
     *  connecting clients while this lives; zero requires none, for a client compiled into by hand. */
    PanoramaMenu(const Services& services, MenuLayout& layout, uint64_t addonId);

    /** Whether @p slot can see the layout: the screens are available and every required addon downloaded. */
    [[nodiscard]] bool CanShow(int slot) const;

    bool Start(int slot, std::shared_ptr<Menu> menu, MenuOptions options) override;
    void Open(int slot, std::shared_ptr<Menu> menu) override;
    [[nodiscard]] bool IsOpen(int slot) const override;
    void Close(int slot) override;
    void CloseAll(int slot) override;
    void CloseAll(int slot, std::string_view replyKey) override;
    void Prompt(int slot, std::string prompt, std::function<bool(int slot, std::string_view text)> callback) override;
    [[nodiscard]] std::string Translate(int slot, std::string_view key, std::string_view fallback) const override;

private:
    /** A sidebar tab and the root row it opens, read once when the session starts. */
    struct Tab
    {
        int RootIndex;
        std::string Label;
        std::string Icon;
    };

    struct Session
    {
        std::vector<Tab> Tabs;
        /** The tab the open branch was entered through, or -1. */
        int SelectedTab = -1;
        int Page = 0;
    };

    [[nodiscard]] int ItemAt(int slot, int row) const;
    void ReadTabs(int slot);

    /** Show the layout and draw the session on it; a player it cannot reach loses the session. */
    void Draw(int slot);
    void DrawTabs(int slot);
    void DrawRows(int slot, const Menu& menu);
    void DrawPrompt(int slot);

    void OnPress(const ButtonPress& press);
    /** Run item @p index of the open menu, remembering which tab it belongs to. */
    void Activate(int slot, int index);
    void StepRow(int slot, int row, int direction);
    void OpenTab(int slot, int tab);
    void TurnPage(int slot, int delta);

    /** Take the menu off @p slot's screen, cancel its prompt, and release its movement. */
    void Hide(int slot);

    Services _services;
    MenuLayout& _layout;
    /** The addon requirement, held while this lives. */
    Subscription _addon;
    MenuStack _stack;
    PerSlot<Session> _sessions;
    /** Declared last: press delivery drops before the state it touches. */
    Subscriptions _subs;
};

}  // namespace VoltMod
