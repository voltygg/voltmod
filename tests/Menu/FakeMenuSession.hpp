#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VoltModTests
{

/**
 * @brief A `MenuSession` that records what it was asked for instead of drawing anything.
 *
 * `MenuSession` is SDK-free by design, which is what lets rows, `MenuBuilder` and `Flow` be driven
 * here without an engine: a submenu row's factory, an input row's validation and a whole flow's
 * step order are all observable as calls on one of these.
 */
class FakeMenuSession final : public VoltMod::MenuSession
{
public:
    void Open(int slot, std::shared_ptr<VoltMod::Menu> menu) override
    {
        Slots.push_back(slot);
        Opened.push_back(std::move(menu));
    }

    void Close(int) override { ++Closes; }

    void CloseAll(int) override { ++CloseAlls; }

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

    /** The menu on top of what has been opened, or null when nothing has been. */
    [[nodiscard]] const VoltMod::Menu* Last() const { return Opened.empty() ? nullptr : Opened.back().get(); }

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

    std::vector<std::shared_ptr<VoltMod::Menu>> Opened;
    std::vector<int> Slots;
    int Closes = 0;
    int CloseAlls = 0;
    int Prompts = 0;
    std::string LastPrompt;
    /** The reply key the last aborting `CloseAll` was given. */
    std::string ReplyKey;
    /** The callback an input row handed the session; call it with a chat line. */
    std::function<bool(int, std::string_view)> LastInput;
};

}  // namespace VoltModTests
