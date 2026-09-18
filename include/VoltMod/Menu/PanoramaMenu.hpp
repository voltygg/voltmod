#pragma once

#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Core/Signals/Subscriptions.hpp>
#include <VoltMod/Core/Slots/PerSlot.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Text/Translations.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
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
 * @brief Run menu sessions on a plugin's Panorama @ref MenuLayout.
 *
 * Uses the same @ref MenuStack behavior as @ref CenterHtmlMenu. Root submenus become sidebar tabs.
 * Pass the menu to `runtime.Menus.Prefer` when Panorama should be the preferred surface.
 */
class PanoramaMenu final : public MenuSurface
{
public:
    /** Services used by the menu. They must outlive it. */
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

    /** @p layout must outlive this. @p addonId identifies the workshop addon required by clients;
     *  zero means the layout is already compiled into the client. */
    PanoramaMenu(const Services& services, MenuLayout& layout, uint64_t addonId);

    /** Whether @p slot can see the layout and has downloaded its required addon. */
    [[nodiscard]] bool CanShow(int slot) const;

    bool OpenSession(int slot, std::shared_ptr<Menu> menu, MenuOptions options) override;
    void Open(int slot, std::shared_ptr<Menu> menu) override;
    [[nodiscard]] bool IsOpen(int slot) const override;
    void Close(int slot) override;
    void CloseAll(int slot) override;
    void CloseAll(int slot, std::string_view replyKey) override;
    void Prompt(int slot, std::string prompt, std::function<bool(int slot, std::string_view text)> callback) override;
    [[nodiscard]] std::string Translate(int slot, std::string_view key, std::string_view fallback) const override;

private:
    struct Tab
    {
        int RootIndex;
        std::string Label;
        std::string Icon;
    };

    struct Session
    {
        std::vector<Tab> Tabs;
        int SelectedTab = -1;
        int Page = 0;
    };

    [[nodiscard]] int ItemAt(int slot, int row) const;
    void ReadTabs(int slot);

    /** Draw the session, or close it when the layout cannot reach the player. */
    void Draw(int slot);
    void DrawTabs(int slot);
    void DrawRows(int slot, const Menu& menu);
    void DrawPrompt(int slot);

    void OnPress(const ButtonPress& press);
    void Activate(int slot, int index);
    void StepRow(int slot, int row, int direction);
    void OpenTab(int slot, int tab);
    void TurnPage(int slot, int delta);

    void Hide(int slot);

    Services _services;
    MenuLayout& _layout;
    Subscription _addon;
    MenuStack _stack;
    PerSlot<Session> _sessions;
    /** Declared last so press delivery stops before its target state is destroyed. */
    Subscriptions _subs;
};

}  // namespace VoltMod
