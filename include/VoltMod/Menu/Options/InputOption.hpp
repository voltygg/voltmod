#pragma once

#include <VoltMod/Menu/MenuOption.hpp>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

/**
 * Free-text input. E pauses the menu and routes the player's next chat line to the validator.
 * If the validator returns false the prompt is re-shown until accepted, timed out, or cancelled (R).
 *
 * The activation path lives in `MenuOption.cpp` because it goes through the manager
 * (@ref HtmlMenuManager::BeginInput), whose header cannot be included here.
 */
class InputOption : public MenuOption
{
public:
    using GetFn = std::function<std::string(int)>;
    using SetFn = std::function<bool(int, std::string_view)>;

    InputOption(std::string title, std::string prompt, GetFn get, SetFn set, int maxLength = 64, bool enabled = true)
        : _title(std::move(title)),
          _prompt(std::move(prompt)),
          _get(std::move(get)),
          _set(std::move(set)),
          _maxLength(maxLength)
    {
        _enabled = enabled;
    }

    std::string GetLabel(int slot) const override
    {
        std::string val = _get ? _get(slot) : std::string{};
        if (val.empty())
            return std::format("{}: …", _title);
        return std::format("{}: {}", _title, val);
    }

    void OnActivate(int slot, HtmlMenuManager& menus) override;

private:
    std::string _title;
    std::string _prompt;
    GetFn _get;
    SetFn _set;
    int _maxLength;
};

}  // namespace VoltMod
