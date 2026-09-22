#pragma once

#include <VoltMod/Menu/MenuModel.hpp>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace VoltModTests
{

/**
 * @brief A `MenuSurface` that records what it was asked for instead of drawing anything.
 *
 * `MenuSurface` is SDK-free by design, which is what lets rows, `MenuBuilder` and `Flow` be driven
 * here without an engine: a submenu row's factory, an input row's validation and a whole flow's
 * step order are all observable as calls on one of these.
 */
class FakeMenuSurface final : public VoltMod::MenuSurface
{
public:
    /** Every key Translate was asked for, in order. */
    mutable std::vector<std::string> Translated;

    bool OpenSession(int slot, std::shared_ptr<VoltMod::Menu> menu, VoltMod::MenuOptions) override
    {
        if (!Accepts)
            return false;

        ++SessionsOpened;
        Open(slot, std::move(menu));
        return true;
    }

    void Open(int slot, std::shared_ptr<VoltMod::Menu> menu) override
    {
        Slots.push_back(slot);
        OpenSlots.insert(slot);
        Opened.push_back(std::move(menu));
    }

    bool IsOpen(int slot) const override { return OpenSlots.contains(slot); }

    void Close(int) override { ++Closes; }

    void CloseAll(int slot) override
    {
        ++CloseAlls;
        OpenSlots.erase(slot);
    }

    void CloseAll(int slot, std::string_view replyKey) override
    {
        ReplyKey = std::string(replyKey);
        CloseAll(slot);
    }

    void Prompt(int, std::string prompt, std::function<bool(int, std::string_view)> callback) override
    {
        ++Prompts;
        LastPrompt = std::move(prompt);
        LastInput = std::move(callback);
    }

    /** No translation table here, so a key always resolves to the framework's own fallback -
     *  which is what makes the default confirm labels observable in a test. */
    std::string Translate(int, std::string_view key, std::string_view fallback) const override
    {
        Translated.emplace_back(key);
        return std::string(fallback);
    }

    /** The menu on top of what has been opened, or null when nothing has been. */
    const VoltMod::Menu* Last() const { return Opened.empty() ? nullptr : Opened.back().get(); }

    /** Press row @p index of the last opened menu, as a driver would. */
    void Press(int index)
    {
        const VoltMod::Menu* menu = Last();
        if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
            return;
        // Copied out first: activating may open a menu, which reallocates Opened.
        const VoltMod::MenuItem item = menu->Items[static_cast<std::size_t>(index)];
        if (item.Activate)
            item.Activate(0, *this);
    }

    /** False makes OpenSession refuse, as a surface that cannot draw for the player does. */
    bool Accepts = true;

    std::vector<std::shared_ptr<VoltMod::Menu>> Opened;
    std::vector<int> Slots;
    /** Slots with a session: opened and not closed with CloseAll. */
    std::set<int> OpenSlots;
    int SessionsOpened = 0;
    int Closes = 0;
    int CloseAlls = 0;
    int Prompts = 0;
    std::string LastPrompt;
    /** The reply key the last aborting `CloseAll` was given. */
    std::string ReplyKey;
    /** The callback an input row handed the surface; call it with a chat line. */
    std::function<bool(int, std::string_view)> LastInput;
};

}  // namespace VoltModTests
